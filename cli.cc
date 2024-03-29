/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"
#include <gtkmm.h>
#include <giomm.h>
#include <iostream>
#include <tiffio.h>
#include <cstring>
#include <cstdlib>
#include <locale.h>
#include "../rtengine/procparams.h"
#include "../rtengine/profilestore.h"
#include "../rtengine/improcfun.h"
#include "options.h"
#include "soundman.h"
#include "rtimage.h"
#include "version.h"
#include "extprog.h"
#include "pathutils.h"

#ifndef WIN32
#include <glibmm/fileutils.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/threads.h>
#else
#include <windows.h>
#include <shlobj.h>
#include <glibmm/thread.h>
#include "conio.h"
#endif

extern Options options;

// stores path to data files
Glib::ustring argv0;
Glib::ustring creditsPath;
Glib::ustring licensePath;
Glib::ustring argv1;

class Worker : public rtengine::ProgressListener
{
private:
    rtengine::InitialImage* image;
    rtengine::StagedImageProcessor* ipc;
    char* path;
    int x, y;
    float mL;
public:
    Worker(rtengine::InitialImage* ii, char* p, int xc, int yc, float minL) {
        image = ii;
        path = p;
        ipc = rtengine::StagedImageProcessor::create(image);
        ipc->setProgressListener(this);
        ipc->setPreviewScale(10);
        x = xc;
        y = yc;
        mL = minL;
    }

    ~Worker()
    {
        rtengine::StagedImageProcessor::destroy(ipc);
    }

    void work()
    {
        std::cout << "Loaded image.." << std::endl;
        rtengine::procparams::ProcParams* params  = new rtengine::procparams::ProcParams();
        rtengine::procparams::ProcParams* ipcParams = ipc->beginUpdateParams();
        // TODO: modify params ?

        rtengine::procparams::PartialProfile* profile = getPartialProfile();
        if (profile != nullptr) {
            profile->applyTo (params);
            profile->deleteInstance();
            delete profile;
        }

        *ipcParams = *params;

        ipc->endUpdateParams(rtengine::ProcEventCode::EvPhotoLoaded);
    }

    rtengine::procparams::PartialProfile* getPartialProfile()
    {
        rtengine::procparams::PartialProfile* rawParams;

        rawParams = new rtengine::procparams::PartialProfile (true, true);
        Glib::ustring profPath = options.findProfilePath (options.defProfRaw);

        if (options.is_defProfRawMissing() || profPath.empty() || (profPath != DEFPROFILE_DYNAMIC && rawParams->load (profPath == DEFPROFILE_INTERNAL ? DEFPROFILE_INTERNAL : Glib::build_filename (profPath, Glib::path_get_basename (options.defProfRaw) + paramFileExtension)))) {
            std::cerr << "Error: default raw processing profile not found." << std::endl;
            rawParams->deleteInstance();
            delete rawParams;
            // deleteProcParams (processingParams);
            return nullptr;
        }

        return rawParams;
    }

    rtengine::IImagefloat* adjustExposure(const rtengine::procparams::ProcParams &params, float &LAB_l)
    {
        // create a processing job with the loaded image and the current processing parameters
        rtengine::ProcessingJob* job = rtengine::ProcessingJob::create (image, params);

        // process image. The error is given back in errorcode.
        int errorCode;
        rtengine::IImagefloat* res = rtengine::processImage (job, errorCode, nullptr);

        float r, g, bl;
        res->getPipetteData(r, g, bl, 50, 50, 8, 0);

        float LAB_a, LAB_b;
        rtengine::Color::rgb2lab01(params.icm.outputProfile, params.icm.workingProfile, r / 65535.f, g / 65535.f, bl / 65535.f, LAB_l, LAB_a, LAB_b, options.rtSettings.HistogramWorking);
        std::cout << LAB_l << ", " << LAB_a << ", " << LAB_b << std::endl;

        return res;
    }

    void saveImage(double temp, double green)
    {
        // TODO: cleanup
        rtengine::procparams::ProcParams params;
        ipc->getParams(&params);

        // params.wb.method = rtengine::procparams::WBEntry::Type::CUSTOM; // temp;
        params.wb.method = "Custom";
        params.wb.temperature = temp;
        // params.toneCurve.expcomp = .6f;
        params.wb.green = green;

        rtengine::procparams::CropParams crop = params.crop;

        params.crop.enabled = true;
        params.crop.x = x - 50;
        params.crop.y = y - 50;
        params.crop.w = params.crop.h = 100;

        float LAB_l, LAB_l_prev;
        float minL = mL;
        float maxL = minL + .5f;

        // get the initial value
        rtengine::IImagefloat* res = adjustExposure(params, LAB_l_prev);
        float increment = LAB_l_prev < minL ? .05f : -(.05f);

        // compute how much one point moves us
        params.toneCurve.expcomp += increment;
        res = adjustExposure(params, LAB_l);

        // try to match it
        while (LAB_l < minL || LAB_l > maxL) {
            // TODO: do not get stuck in an infinite loop
            float diff = abs(LAB_l - LAB_l_prev);
            float toGo = abs(minL - LAB_l);
            std::cout << "diff:" << diff << "|toGo:" << toGo << std::endl;

            increment = LAB_l_prev < minL ? .05f : -(.05f);
            params.toneCurve.expcomp += std::max(increment * toGo / diff, increment);
            LAB_l_prev = LAB_l;
            res = adjustExposure(params, LAB_l);
        }

        params.crop.enabled = false;
        res = adjustExposure(params, LAB_l);
        
        // save image to disk
        res->saveToFile (path);
        // save profile as well
        params.save(strcat(path, ".pp3"));
    }
    
    void setProgressStr(const Glib::ustring& str)
    {
        std::cout << str << std::endl;
    }

    void setProgress (double p)
    {
        std::cout << p << std::endl;
    }

    void setProgressState(bool inProcessing)
    {
        std::cout << inProcessing << std::endl;
        // get the white balance from one spot
        if (inProcessing == false)
        {
            double temp, green;
            ipc->getSpotWB(x, y, 8, temp, green);

            std::cout << "temp:" << temp << "|green:" << green<< std::endl;
            
            saveImage(temp, green);
        }
    }

    void error(const Glib::ustring& descr)
    {
    }
};

int init (char* argv[]);

int main (int argc, char* argv[])
{
    int code = init(argv);

    if (code != 0) {
        exit(code);
    }

    if (argc < 6) {
        std::cout << "Usage: rtcmd <infile> <outfile> <x> <y> <minL>" << std::endl;
        exit(1);
    }

    // Load the image given in the first command line parameter
    rtengine::InitialImage* ii;
    int errorCode;
    
    ii = rtengine::InitialImage::load (argv[1], true, &errorCode, nullptr);

    if (!ii) {
        std::cout << "Input file not supported." << std::endl;
        exit(2);
    }

    Worker* worker = new Worker(ii, argv[2], atoi(argv[3]), atoi(argv[4]), atof(argv[5]));
    worker->work();

    delete worker;
    // delete ii;

    // TODO: cleanup
    // rtengine::procparams::ProcParams params;

    // create a processing job with the loaded image and the current processing parameters
    // rtengine::ProcessingJob* job = rtengine::ProcessingJob::create (ii, params);

    // process image. The error is given back in errorcode.
    // rtengine::IImagefloat* res = rtengine::processImage (job, errorCode, nullptr);

    // save image to disk
    // res->saveToFile (argv[2]);
}

int init (char* argv[])
{
    setlocale (LC_ALL, "");
    setlocale (LC_NUMERIC, "C"); // to set decimal point to "."

    Gio::init ();

#ifdef BUILD_BUNDLE
    char exname[512] = {0};
    Glib::ustring exePath;
    // get the path where the rawtherapee executable is stored
#ifdef WIN32
    WCHAR exnameU[512] = {0};
    GetModuleFileNameW (NULL, exnameU, 511);
    WideCharToMultiByte (CP_UTF8, 0, exnameU, -1, exname, 511, 0, 0 );
#else

    if (readlink ("/proc/self/exe", exname, 511) < 0) {
        strncpy (exname, argv[0], 511);
    }

#endif
    exePath = Glib::path_get_dirname (exname);

    // set paths
    if (Glib::path_is_absolute (DATA_SEARCH_PATH)) {
        argv0 = DATA_SEARCH_PATH;
    } else {
        argv0 = Glib::build_filename (exePath, DATA_SEARCH_PATH);
    }

    if (Glib::path_is_absolute (CREDITS_SEARCH_PATH)) {
        creditsPath = CREDITS_SEARCH_PATH;
    } else {
        creditsPath = Glib::build_filename (exePath, CREDITS_SEARCH_PATH);
    }

    if (Glib::path_is_absolute (LICENCE_SEARCH_PATH)) {
        licensePath = LICENCE_SEARCH_PATH;
    } else {
        licensePath = Glib::build_filename (exePath, LICENCE_SEARCH_PATH);
    }

    options.rtSettings.lensfunDbDirectory = LENSFUN_DB_PATH;

#else
    argv0 = DATA_SEARCH_PATH;
    creditsPath = CREDITS_SEARCH_PATH;
    licensePath = LICENCE_SEARCH_PATH;
    options.rtSettings.lensfunDbDirectory = LENSFUN_DB_PATH;
#endif

    try {
        Options::load (false);
    } catch (Options::Error &e) {
        std::cerr << std::endl << "FATAL ERROR:" << std::endl << e.get_msg() << std::endl;
        return -2;
    }

    if (options.is_defProfRawMissing()) {
        options.defProfRaw = DEFPROFILE_RAW;
        std::cerr << std::endl
                  << "The default profile for raw photos could not be found or is not set." << std::endl
                  << "Please check your profiles' directory, it may be missing or damaged." << std::endl
                  << "\"" << DEFPROFILE_RAW << "\" will be used instead." << std::endl << std::endl;
    }

    if (options.is_bundledDefProfRawMissing()) {
        std::cerr << std::endl
                  << "The bundled profile \"" << options.defProfRaw << "\" could not be found!" << std::endl
                  << "Your installation could be damaged." << std::endl
                  << "Default internal values will be used instead." << std::endl << std::endl;
        options.defProfRaw = DEFPROFILE_INTERNAL;
    }

    if (options.is_defProfImgMissing()) {
        options.defProfImg = DEFPROFILE_IMG;
        std::cerr << std::endl
                  << "The default profile for non-raw photos could not be found or is not set." << std::endl
                  << "Please check your profiles' directory, it may be missing or damaged." << std::endl
                  << "\"" << DEFPROFILE_IMG << "\" will be used instead." << std::endl << std::endl;
    }

    if (options.is_bundledDefProfImgMissing()) {
        std::cerr << std::endl
                  << "The bundled profile " << options.defProfImg << " could not be found!" << std::endl
                  << "Your installation could be damaged." << std::endl
                  << "Default internal values will be used instead." << std::endl << std::endl;
        options.defProfImg = DEFPROFILE_INTERNAL;
    }

    TIFFSetWarningHandler (nullptr);   // avoid annoying message boxes

#ifndef WIN32

    // Move the old path to the new one if the new does not exist
    if (Glib::file_test (Glib::build_filename (options.rtdir, "cache"), Glib::FILE_TEST_IS_DIR) && !Glib::file_test (options.cacheBaseDir, Glib::FILE_TEST_IS_DIR)) {
        if (g_rename (Glib::build_filename (options.rtdir, "cache").c_str (), options.cacheBaseDir.c_str ()) == -1) {
            std::cout << "g_rename " <<  Glib::build_filename (options.rtdir, "cache").c_str () << " => " << options.cacheBaseDir.c_str () << " failed." << std::endl;
        }
    }

#endif

    // printing RT's version in all case, particularly useful for the 'verbose' mode, but also for the batch processing
    std::cout << "RawTherapee, version " << RTVERSION << ", command line." << std::endl;

    return 0;
}
