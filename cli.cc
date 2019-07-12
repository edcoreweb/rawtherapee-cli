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
#include "../rtengine/improccoordinator.h"
#include "../rtengine/refreshmap.h"
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

class IPC : public rtengine::ImProcCoordinator
{
public:
    void process(int changeFlags)
    {
        changeSinceLast |= changeFlags;

        paramsUpdateMutex.unlock();
        rtengine::ImProcCoordinator::process();
    }
};

class Worker : public rtengine::ProgressListener, public rtengine::DetailedCropListener
{
private:
    rtengine::InitialImage* image;
    IPC* ipc;
    char* path;
    int x, y;
    float mL;
    bool adjustedWB = false;
    bool adjustingExposure = true;
    float LAB_l = 0, LAB_l_prev = 0;

    rtengine::IImage8 *cropImage;
    rtengine::DetailedCrop* crop;
public:
    Worker(rtengine::InitialImage* ii, char* p, int xc, int yc, float minL)
    {
        image = ii;
        path = p;
        ipc = new IPC();
        ipc->assign(image->getImageSource());

        ipc->setProgressListener(this);
        // ipc->setPreviewScale(10);
        crop = ipc->createCrop(nullptr, false);
        crop->setListener(this);

        x = xc;
        y = yc;
        mL = minL;
    }

    ~Worker()
    {
        if (cropImage) {
            cropImage->free();
        }

        delete crop;

        // TODO: memory leak ?
        // delete ipc;
        // rtengine::StagedImageProcessor::destroy(static_cast<rtengine::StagedImageProcessor*>(ipc));
    }

    void setDetailedCrop(
        rtengine::IImage8* img, rtengine::IImage8* imgtrue,
        const rtengine::procparams::ColorManagementParams& cmp, const rtengine::procparams::CropParams& cp,
        int cx, int cy, int cw, int ch, int skip
    ) {
        if (cropImage) {
            cropImage->free();
        }

        cropImage = new rtengine::Image8(100, 100);
        imgtrue->copyData(cropImage);
    }

    void getPipetteData(float &valueR, float &valueG, float &valueB, int posX, int posY, const int squareSize, int tran)
    {
        int x;
        int y;
        int width = cropImage->getWidth();
        int height = cropImage->getHeight();
        float accumulatorR = 0.f;  // using float to avoid range overflow; -> please creates specialization if necessary
        float accumulatorG = 0.f;  //    "
        float accumulatorB = 0.f;  //    "
        unsigned long int n = 0;
        int halfSquare = squareSize / 2;
        cropImage->transformPixel (posX, posY, tran, x, y);

        for (int iy = y - halfSquare; iy < y - halfSquare + squareSize; ++iy) {
            for (int ix = x - halfSquare; ix < x - halfSquare + squareSize; ++ix) {
                if (ix >= 0 && iy >= 0 && ix < width && iy < height) {
                    accumulatorR += float(cropImage->r(iy, ix));
                    accumulatorG += float(cropImage->g(iy, ix));
                    accumulatorB += float(cropImage->b(iy, ix));
                    ++n;
                }
            }
        }

        valueR = n ? float(accumulatorR / float(n)) : float(0);
        valueG = n ? float(accumulatorG / float(n)) : float(0);
        valueB = n ? float(accumulatorB / float(n)) : float(0);
    }

    void getWindow(int& cx, int& cy, int& cw, int& ch, int& skip)
    {
        cx = x - 50;
        cy = y - 50;
        cw = 100;
        ch = 100;
        skip = 1;
    }

    void work()
    {
        std::cout << "Loaded image.." << std::endl;
        rtengine::procparams::ProcParams* params  = new rtengine::procparams::ProcParams();
        rtengine::procparams::ProcParams* ipcParams = ipc->beginUpdateParams();

        rtengine::procparams::PartialProfile* profile = getPartialProfile();
        if (profile != nullptr) {
            profile->applyTo (params);
            profile->deleteInstance();
            delete profile;
        }

        *ipcParams = *params;
        delete params;

        ipc->process(ALL);
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

    void adjustExposure(const rtengine::procparams::ProcParams* params, float &LAB_l)
    {
        float r, g, bl;
        getPipetteData(r, g, bl, 50, 50, 8, 0);

        float LAB_a, LAB_b;
        rtengine::Color::rgb2lab01(params->icm.outputProfile, params->icm.workingProfile, r / 255, g / 255, bl / 255, LAB_l, LAB_a, LAB_b, options.rtSettings.HistogramWorking);
        std::cout << LAB_l << ", " << LAB_a << ", " << LAB_b << std::endl;
    }

    void saveImage()
    {
        rtengine::procparams::ProcParams* params = ipc->beginUpdateParams();

        if (adjustedWB == false)
        {
            adjustedWB = true;
            double temp, green;
            ipc->getSpotWB(x, y, 8, temp, green);
            std::cout << "temp:" << temp << "|green:" << green<< std::endl;

            // params.wb.method = rtengine::procparams::WBEntry::Type::CUSTOM; // temp;
            params->wb.method = "Custom";
            params->wb.temperature = temp;
            // params.toneCurve.expcomp = .6f;
            params->wb.green = green;
            
            ipc->process(ALLNORAW);
            return;
        }

        float minL = mL;
        float maxL = minL + .5f;
        adjustingExposure = LAB_l == 0 || LAB_l < minL || LAB_l > maxL;

        if (adjustingExposure == true)
        {
            // // get the initial value
            LAB_l_prev = LAB_l;
            adjustExposure(params, LAB_l);
            float increment = LAB_l < minL ? .05f : -(.05f);
            std::cout << "l:" << LAB_l << "|lprev:" << LAB_l_prev << std::endl;

            if (LAB_l_prev == 0)
            {
                params->toneCurve.expcomp += increment;
                ipc->process(AUTOEXP);
                return;
            }

            float diff = abs(LAB_l - LAB_l_prev);
            float toGo = abs(minL - LAB_l);
            std::cout << "diff:" << diff << "|toGo:" << toGo << std::endl;

            params->toneCurve.expcomp += std::max(increment * toGo / diff, increment);

            ipc->process(AUTOEXP);
            return;
        }

        params->resize.enabled = true;
        params->resize.width = 1920;
        params->resize.height = 1080;

        // create a processing job with the loaded image and the current processing parameters
        rtengine::ProcessingJob* job = rtengine::ProcessingJob::create (image, *params);

        // process image. The error is given back in errorcode.
        int errorCode;
        rtengine::IImagefloat* res = rtengine::processImage (job, errorCode, nullptr);
        
        // // save image to disk
        res->saveToFile (path);
        // // save profile as well
        params->save(strcat(path, ".pp3"));

        delete res;
    }
    
    void setProgressStr(const Glib::ustring& str)
    {}

    void setProgress (double p)
    {}

    void error(const Glib::ustring& descr)
    {}

    void setProgressState(bool inProcessing)
    {
        // get the white balance from one spot
        if (inProcessing == false)
        {
            saveImage();
        }
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
    delete ii;
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
