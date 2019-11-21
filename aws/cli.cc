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

    rtengine::IImage8 *cropImage = nullptr;
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
        if (params) {
            delete params;
        }

        if (ipc != nullptr) {
            ipc->process(ALL);
        }

        if (ipcParams) {
            delete ipcParams;
        }
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

/* Process line command options
 * Returns
 *  0 if process in batch has executed
 *  1 to start GUI (with a dir or file option)
 *  2 to start GUI because no files found
 *  -1 if there is an error in parameters
 *  -2 if an error occurred during processing
 *  -3 if at least one required procparam file was not found */
int processLineParams ( int argc, char **argv );

bool dontLoadCache ( int argc, char **argv );

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

    if (argc == 6) {
        // calibrate image for point
        // Load the image given in the first command line parameter
        rtengine::InitialImage* ii;
        int errorCode;

        ii = rtengine::InitialImage::load (argv[1], true, &errorCode, nullptr);

        if (!ii) {
//            std::cout << "Input file not supported." << std::endl;
            exit(2);
        }

        Worker* worker = new Worker(ii, argv[2], atoi(argv[3]), atoi(argv[4]), atof(argv[5]));
        worker->work();

        delete worker;
        delete ii;
    } else if (argc > 6) {
        // generate thumbnail
        processLineParams (argc, argv);
    }
}

void deleteProcParams (std::vector<rtengine::procparams::PartialProfile*> &pparams)
{
    for (unsigned int i = 0; i < pparams.size(); i++) {
        pparams[i]->deleteInstance();
        delete pparams[i];
        pparams[i] = NULL;
    }

    return;
}


bool dontLoadCache ( int argc, char **argv )
{
    for (int iArg = 1; iArg < argc; iArg++) {
        Glib::ustring currParam (argv[iArg]);

        if ( currParam.length() > 1 && currParam.at(0) == '-' && currParam.at(1) == 'q' ) {
            return true;
        }
    }

    return false;
}

int processLineParams ( int argc, char **argv )
{
    rtengine::procparams::PartialProfile *rawParams = nullptr, *imgParams = nullptr;
    std::vector<Glib::ustring> inputFiles;
    Glib::ustring outputPath;
    std::vector<rtengine::procparams::PartialProfile*> processingParams;
    bool outputDirectory = false;
    bool leaveUntouched = false;
    bool overwriteFiles = false;
    bool sideProcParams = false;
    bool copyParamsFile = false;
    bool skipIfNoSidecar = false;
    bool allExtensions = false;
    bool useDefault = false;
    unsigned int sideCarFilePos = 0;
    int compression = 92;
    int subsampling = 3;
    int bits = -1;
    bool isFloat = false;
    std::string outputType;
    unsigned errors = 0;
    int rawWidth = 0;
    int rawHeight = 0;
    int resultImageWidth = 0;
    int resultImageHeight = 0;

    for ( int iArg = 1; iArg < argc; iArg++) {
        Glib::ustring currParam (argv[iArg]);
        if ( currParam.empty() ) {
            continue;
        }

        if ( currParam.at (0) == '-' && currParam.size() > 1) {
            switch ( currParam.at (1) ) {
                case '-':
                    // GTK --argument, we're skipping it
                    break;

                case 'o': // outputfile or dir
                    if ( iArg + 1 < argc ) {
                        iArg++;
                        outputPath = Glib::ustring (Glib::filename_to_utf8 (argv[iArg]));

                        if (outputPath.substr (0, 9) == "/dev/null") {
                            outputPath.assign ("/dev/null"); // removing any useless chars or filename
                            outputDirectory = false;
                            leaveUntouched = true;
                        } else if (Glib::file_test (outputPath, Glib::FILE_TEST_IS_DIR)) {
                            outputDirectory = true;
                        }
                    }

                    break;

                case 'p': // processing parameters for all inputs; all set procparams are required, so

                    // RT stop if any of them can't be loaded for any reason.
                    if ( iArg + 1 < argc ) {
                        iArg++;
                        Glib::ustring fname (Glib::filename_to_utf8 (argv[iArg]));

                        if (fname.at (0) == '-') {
                            // std::cerr << "Error: filename missing next to the -p switch." << std::endl;
                            deleteProcParams (processingParams);
                            return -3;
                        }

                        rtengine::procparams::PartialProfile* currentParams = new rtengine::procparams::PartialProfile (true);

                        if (! (currentParams->load ( fname ))) {
                            processingParams.push_back (currentParams);
                        } else {
                            // std::cerr << "Error: \"" << fname << "\" not found." << std::endl;
                            deleteProcParams (processingParams);
                            return -3;
                        }
                    }

                    break;

                case 'd':
                    useDefault = true;
                    break;

                case 'Y':
                    overwriteFiles = true;
                    break;

                case 'c': // MUST be last option
                    while (iArg + 1 < argc) {
                        iArg++;
                        Glib::ustring argument (Glib::filename_to_utf8 (argv[iArg]));

                        if (!Glib::file_test (argument, Glib::FILE_TEST_EXISTS)) {
                            // std::cout << "\"" << argument << "\"  doesn't exist!" << std::endl;
                            continue;
                        }

                        if (Glib::file_test (argument, Glib::FILE_TEST_IS_REGULAR)) {
                            bool notAll = allExtensions && !options.is_parse_extention (argument);
                            bool notRetained = !allExtensions && !options.has_retained_extention (argument);

                            if (notAll || notRetained) {
                                if (notAll) {
                                    // std::cout << "\"" << argument << "\"  is not one of the parsed extensions. Image skipped." << std::endl;
                                } else if (notRetained) {
                                    // std::cout << "\"" << argument << "\"  is not one of the selected parsed extensions. Image skipped." << std::endl;
                                }
                            } else {
                                inputFiles.emplace_back (argument);
                            }

                            continue;

                        }

                        if (Glib::file_test (argument, Glib::FILE_TEST_IS_DIR)) {

                            auto dir = Gio::File::create_for_path (argument);

                            if (!dir || !dir->query_exists()) {
                                continue;
                            }

                            try {

                                auto enumerator = dir->enumerate_children ("standard::name,standard::type");

                                while (auto file = enumerator->next_file()) {

                                    const auto fileName = Glib::build_filename (argument, file->get_name());
                                    bool isDir = file->get_file_type() == Gio::FILE_TYPE_DIRECTORY;
                                    bool notAll = allExtensions && !options.is_parse_extention (fileName);
                                    bool notRetained = !allExtensions && !options.has_retained_extention (fileName);

                                    if (isDir || notAll || notRetained) {
                                        if (isDir) {
                                            // std::cout << "\"" << fileName << "\"  is a folder. Folder skipped" << std::endl;
                                        } else if (notAll) {
                                            // std::cout << "\"" << fileName << "\"  is not one of the parsed extensions. Image skipped." << std::endl;
                                        } else if (notRetained) {
                                            // std::cout << "\"" << fileName << "\"  is not one of the selected parsed extensions. Image skipped." << std::endl;
                                        }

                                        continue;

                                    }

                                    if (sideProcParams && skipIfNoSidecar) {
                                        // look for the sidecar proc params
                                        if (!Glib::file_test (fileName + paramFileExtension, Glib::FILE_TEST_EXISTS)) {
                                            // std::cout << "\"" << fileName << "\"  has no side-car file. Image skipped." << std::endl;
                                            continue;
                                        }
                                    }

                                    inputFiles.emplace_back (fileName);
                                }

                            } catch (Glib::Exception&) {}

                            continue;
                        }

                        // std::cerr << "\"" << argument << "\" is neither a regular file nor a directory." << std::endl;
                    }

                    break;

                default: {
                    break;
                }
            }
        } else {
            argv1 = Glib::ustring (Glib::filename_to_utf8 (argv[iArg]));

            if ( outputDirectory ) {
                options.savePathFolder = outputPath;
                options.saveUsePathTemplate = false;
            } else {
                options.saveUsePathTemplate = true;

                if (options.savePathTemplate.empty())
                    // If the save path template is empty, we use its default value
                {
                    options.savePathTemplate = "%p1/converted/%f";
                }
            }

            if (outputType == "jpg") {
                options.saveFormat.format = outputType;
                options.saveFormat.jpegQuality = compression;
                options.saveFormat.jpegSubSamp = subsampling;
            } else if (outputType == "tif") {
                options.saveFormat.format = outputType;
            } else if (outputType == "png") {
                options.saveFormat.format = outputType;
            }

            break;
        }
    }

    if (bits == -1) {
        if (outputType == "jpg") {
            bits = 8;
        } else if (outputType == "png") {
            bits = 8;
        } else if (outputType == "tif") {
            bits = 16;
        } else {
            bits = 8;
        }
    }

    if ( !argv1.empty() ) {
        return 1;
    }

    if ( inputFiles.empty() ) {
        return 2;
    }

    if (useDefault) {
        rawParams = new rtengine::procparams::PartialProfile (true, true);
        Glib::ustring profPath = options.findProfilePath (options.defProfRaw);

        // std::cout << "Prof: " << profPath << std::endl;

        if (options.is_defProfRawMissing() || profPath.empty() || (profPath != DEFPROFILE_DYNAMIC && rawParams->load (profPath == DEFPROFILE_INTERNAL ? DEFPROFILE_INTERNAL : Glib::build_filename (profPath, Glib::path_get_basename (options.defProfRaw) + paramFileExtension)))) {
            // std::cerr << "Error: default raw processing profile not found." << std::endl;
            rawParams->deleteInstance();
            delete rawParams;
            deleteProcParams (processingParams);
            return -3;
        }

        imgParams = new rtengine::procparams::PartialProfile (true);
        profPath = options.findProfilePath (options.defProfImg);

        if (options.is_defProfImgMissing() || profPath.empty() || (profPath != DEFPROFILE_DYNAMIC && imgParams->load (profPath == DEFPROFILE_INTERNAL ? DEFPROFILE_INTERNAL : Glib::build_filename (profPath, Glib::path_get_basename (options.defProfImg) + paramFileExtension)))) {
            // std::cerr << "Error: default non-raw processing profile not found." << std::endl;
            imgParams->deleteInstance();
            delete imgParams;
            rawParams->deleteInstance();
            delete rawParams;
            deleteProcParams (processingParams);
            return -3;
        }
    }

    for ( size_t iFile = 0; iFile < inputFiles.size(); iFile++) {

        // Has to be reinstanciated at each profile to have a ProcParams object with default values
        rtengine::procparams::ProcParams currentParams;

        Glib::ustring inputFile = inputFiles[iFile];
        std::cout << "Output is " << bits << "-bit " << (isFloat ? "floating-point" : "integer") << "." << std::endl;
        std::cout << "Processing: " << inputFile << std::endl;

        rtengine::InitialImage* ii = nullptr;
        rtengine::ProcessingJob* job = nullptr;
        int errorCode;
        bool isRaw = false;

        Glib::ustring outputFile;

        if ( outputType.empty() ) {
            outputType = "jpg";
        }

        if ( outputPath.empty() ) {
            Glib::ustring s = inputFile;
            Glib::ustring::size_type ext = s.find_last_of ('.');
            outputFile = s.substr (0, ext) + "." + outputType;
        } else if ( outputDirectory ) {
            Glib::ustring s = Glib::path_get_basename ( inputFile );
            Glib::ustring::size_type ext = s.find_last_of ('.');
            outputFile = Glib::build_filename (outputPath, s.substr (0, ext) + "." + outputType);
        } else {
            if (leaveUntouched) {
                outputFile = outputPath;
            } else {
                Glib::ustring s = outputPath;
                Glib::ustring::size_type ext = s.find_last_of ('.');
                outputFile = s.substr (0, ext) + "." + outputType;
            }
        }

        if ( inputFile == outputFile) {
            // std::cerr << "Cannot overwrite: " << inputFile << std::endl;
            continue;
        }

        if ( !overwriteFiles && Glib::file_test ( outputFile, Glib::FILE_TEST_EXISTS ) ) {
            // std::cerr << outputFile  << " already exists: use -Y option to overwrite. This image has been skipped." << std::endl;
            continue;
        }

        // Load the image
        isRaw = true;
        Glib::ustring ext = getExtension (inputFile);

        if (ext.lowercase() == "jpg" || ext.lowercase() == "jpeg" || ext.lowercase() == "tif" || ext.lowercase() == "tiff" || ext.lowercase() == "png") {
            isRaw = false;
        }

        ii = rtengine::InitialImage::load ( inputFile, isRaw, &errorCode, nullptr );

        if (!ii) {
            errors++;
            // std::cerr << "Error loading file: " << inputFile << std::endl;
            continue;
        }

        ii->getImageSource()->getFullSize(rawWidth, rawHeight);

        rtengine::procparams::ColorManagementParams cmp = nullptr;
        rtengine::procparams::CropParams cp = nullptr;
        // std::cout << "prof1: " << ii->getImageSource()->getDCP(cmp, cp) << std::endl;

        if (useDefault) {
            if (isRaw) {
                if (options.defProfRaw == DEFPROFILE_DYNAMIC) {
                    rawParams->deleteInstance();
                    delete rawParams;
                    rawParams = ProfileStore::getInstance()->loadDynamicProfile (ii->getMetaData());
                }

                // std::cout << "  Merging default raw processing profile." << std::endl;

                rawParams->applyTo (&currentParams);
            } else {
                if (options.defProfImg == DEFPROFILE_DYNAMIC) {
                    imgParams->deleteInstance();
                    delete imgParams;
                    imgParams = ProfileStore::getInstance()->loadDynamicProfile (ii->getMetaData());
                }

                // std::cout << "  Merging default non-raw processing profile." << std::endl;
                imgParams->applyTo (&currentParams);
            }
        }

        bool sideCarFound = false;
        unsigned int i = 0;

        // Iterate the procparams file list in order to build the final ProcParams
        do {
            if (sideProcParams && i == sideCarFilePos) {
                // using the sidecar file
                Glib::ustring sideProcessingParams = inputFile + paramFileExtension;

                // the "load" method don't reset the procparams values anymore, so values found in the procparam file override the one of currentParams
                if ( !Glib::file_test ( sideProcessingParams, Glib::FILE_TEST_EXISTS ) || currentParams.load ( sideProcessingParams )) {
                    // std::cerr << "Warning: sidecar file requested but not found for: " << sideProcessingParams << std::endl;
                } else {
                    sideCarFound = true;
                    // std::cout << "  Merging sidecar procparams." << std::endl;
                }
            }

            if ( processingParams.size() > i  ) {
                // std::cout << "  Merging procparams #" << i << std::endl;
                processingParams[i]->applyTo (&currentParams);
            }

            i++;
        } while (i < processingParams.size() + (sideProcParams ? 1 : 0));

        if ( sideProcParams && !sideCarFound && skipIfNoSidecar ) {
            delete ii;
            errors++;
            // std::cerr << "Error: no sidecar procparams found for: " << inputFile << std::endl;
            continue;
        }

        job = rtengine::ProcessingJob::create (ii, currentParams, false);

        if ( !job ) {
            errors++;
            // std::cerr << "Error creating processing for: " << inputFile << std::endl;
            ii->decreaseRef();
            continue;
        }

        // Process image
        rtengine::IImagefloat* resultImage = rtengine::processImage (job, errorCode, nullptr);

        if ( !resultImage ) {
            errors++;
            // std::cerr << "Error processing: " << inputFile << std::endl;
            rtengine::ProcessingJob::destroy ( job );
            continue;
        }

        resultImageWidth = resultImage->getWidth();
        resultImageHeight = resultImage->getHeight();

        // output rawWidth, rawHeight, width, height
        std::cout << "{\"rawWidth\":\"" << rawWidth << "\",";
        std::cout << "\"rawHeight\":\"" << rawHeight << "\",";
        std::cout << "\"width\":\"" << resultImageWidth << "\",";
        std::cout << "\"height\":\"" << resultImageHeight << "\"}";

        // save image to disk
        if ( outputType == "jpg" ) {
            errorCode = resultImage->saveAsJPEG ( outputFile, compression, subsampling );
        } else if ( outputType == "tif" ) {
            errorCode = resultImage->saveAsTIFF ( outputFile, bits, isFloat, compression == 0  );
        } else if ( outputType == "png" ) {
            errorCode = resultImage->saveAsPNG ( outputFile, bits );
        } else {
            errorCode = resultImage->saveToFile (outputFile);
        }

        if (errorCode) {
            errors++;
            // std::cerr << "Error saving to: " << outputFile << std::endl;
        } else {
            if ( copyParamsFile ) {
                Glib::ustring outputProcessingParams = outputFile + paramFileExtension;
                currentParams.save ( outputProcessingParams );
            }
        }

        ii->decreaseRef();
        resultImage->free();
    }

    if (imgParams) {
        imgParams->deleteInstance();
        delete imgParams;
    }

    if (rawParams) {
        rawParams->deleteInstance();
        delete rawParams;
    }

    deleteProcParams (processingParams);

    return errors > 0 ? -2 : 0;
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

    options.profilePath = "/tmp/profiles";
    try {
        Options::load (true);
    } catch (Options::Error &e) {
        // std::cerr << std::endl << "FATAL ERROR:" << std::endl << e.get_msg() << std::endl;
        return -2;
    }

    if (options.is_defProfRawMissing()) {
        options.defProfRaw = DEFPROFILE_RAW;
        // std::cerr << std::endl
        //           << "The default profile for raw photos could not be found or is not set." << std::endl
        //           << "Please check your profiles' directory, it may be missing or damaged." << std::endl
        //           << "\"" << DEFPROFILE_RAW << "\" will be used instead." << std::endl << std::endl;
    }

    if (options.is_bundledDefProfRawMissing()) {
        // std::cerr << std::endl
        //           << "The bundled profile \"" << options.defProfRaw << "\" could not be found!" << std::endl
        //           << "Your installation could be damaged." << std::endl
        //           << "Default internal values will be used instead." << std::endl << std::endl;
        options.defProfRaw = DEFPROFILE_INTERNAL;
    }

    if (options.is_defProfImgMissing()) {
        options.defProfImg = DEFPROFILE_IMG;
        // std::cerr << std::endl
        //           << "The default profile for non-raw photos could not be found or is not set." << std::endl
        //           << "Please check your profiles' directory, it may be missing or damaged." << std::endl
        //           << "\"" << DEFPROFILE_IMG << "\" will be used instead." << std::endl << std::endl;
    }

    if (options.is_bundledDefProfImgMissing()) {
        // std::cerr << std::endl
        //           << "The bundled profile " << options.defProfImg << " could not be found!" << std::endl
        //           << "Your installation could be damaged." << std::endl
        //           << "Default internal values will be used instead." << std::endl << std::endl;
        options.defProfImg = DEFPROFILE_INTERNAL;
    }

    TIFFSetWarningHandler (nullptr);   // avoid annoying message boxes

#ifndef WIN32

    // Move the old path to the new one if the new does not exist
    if (Glib::file_test (Glib::build_filename (options.rtdir, "cache"), Glib::FILE_TEST_IS_DIR) && !Glib::file_test (options.cacheBaseDir, Glib::FILE_TEST_IS_DIR)) {
        if (g_rename (Glib::build_filename (options.rtdir, "cache").c_str (), options.cacheBaseDir.c_str ()) == -1) {
            // std::cout << "g_rename " <<  Glib::build_filename (options.rtdir, "cache").c_str () << " => " << options.cacheBaseDir.c_str () << " failed." << std::endl;
        }
    }

#endif

    // printing RT's version in all case, particularly useful for the 'verbose' mode, but also for the batch processing
    // std::cout << "RawTherapee, version " << RTVERSION << ", command line." << std::endl;

    return 0;
}
