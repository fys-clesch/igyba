#include "ids_cam.h"
#include <assert.h>

ids_cam::ids_cam(void)
{
    /* SENSORINFO */
    s_info = SENSORINFO{0,
                        "",
                        '\0',
                        0, 0,
                        false, false, false, false, false,
                        0,
                        '\0',
                        ""};
    /* BOARDINFO */
    b_info = BOARDINFO{"", "", "", "",
                       '\0', '\0',
                       ""};
    /* HCAM */
    pcam = (HCAM)NULL;
    /* int */
    err = memID = 0; /* 2 */
    /* char * */
    im_mem = (char *)NULL;
    /* double */
    exp_time =
    exp_time_inc =
    exp_time_max =
    exp_time_min =
    fps = 0.;
    dpix_size = -1.; /* 6 */
    /* bool */
    cntrl_exp_time =
    supp_fine_inc_exp_time =
    err_break = false; /* 3 */
    /* uint16_t */
    pixel_format = IS_CM_MONO8; /**< Important for USB 2.0 cameras -- other settings don't work. */
    color_mod_init =
    bits_p_pix =
    pix_size = 0; /* 4 */
    /* atomic<double> */
    fps_atm.store(0., std::memory_order_relaxed);
    dpix_size_atm.store(0., std::memory_order_relaxed);
    exp_time_atm.store(0., std::memory_order_relaxed);
    exp_time_min_atm.store(0., std::memory_order_relaxed);
    exp_time_max_atm.store(0., std::memory_order_relaxed);
    exp_time_inc_atm.store(0., std::memory_order_relaxed); /* 6 */
    /* uint32_t */
    pix_clock =
    pix_clock_min =
    pix_clock_max =
    pix_clock_inc =
    im_width =
    im_max_width =
    im_height =
    im_max_height =
    im_min_width =
    im_min_height =
    im_inc_width =
    im_inc_height =
    sensor_aa_width =
    sensor_aa_height =
    im_aoi_width =
    im_aoi_height =
    im_aoi_width_start =
    im_aoi_height_start =
    used_bandwidth = 0; /* 19 */
    /* string */
    infotbar_win_name  = "camera information window";
    /* Mat */
    infotbar_win_mat = cv::Mat::zeros(150, 350, CV_8UC3);

    /* Set the bits per pixel variable */
    /* @todo This should be done in a stand-alone colour mode setter: */
    if(pixel_format == IS_CM_MONO8 ||
       pixel_format == IS_CM_SENSOR_RAW8)
        bits_p_pix = 8;
    else if(pixel_format == IS_CM_MONO10 ||
            pixel_format == IS_CM_MONO12 ||
            pixel_format == IS_CM_MONO16 ||
            pixel_format == IS_CM_SENSOR_RAW10 ||
            pixel_format == IS_CM_SENSOR_RAW12 ||
            pixel_format == IS_CM_SENSOR_RAW16 ||
            pixel_format == IS_CM_BGR5_PACKED ||
            pixel_format == IS_CM_BGR565_PACKED ||
            pixel_format == IS_CM_UYVY_PACKED ||
            pixel_format == IS_CM_UYVY_MONO_PACKED ||
            pixel_format == IS_CM_UYVY_BAYER_PACKED ||
            pixel_format == IS_CM_CBYCRY_PACKED)
        bits_p_pix = 16;
    else if(pixel_format == IS_CM_RGB8_PACKED ||
            pixel_format == IS_CM_BGR8_PACKED ||
            pixel_format == IS_CM_RGB8_PLANAR)
        bits_p_pix = 24;
    else if(pixel_format == IS_CM_RGBA8_PACKED ||
            pixel_format == IS_CM_BGRA8_PACKED ||
            pixel_format == IS_CM_RGBY8_PACKED ||
            pixel_format == IS_CM_BGRY8_PACKED ||
            pixel_format == IS_CM_RGB10_PACKED ||
            pixel_format == IS_CM_BGR10_PACKED)
        bits_p_pix = 32;
    else if(pixel_format == IS_CM_RGB10_UNPACKED ||
            pixel_format == IS_CM_BGR10_UNPACKED ||
            pixel_format == IS_CM_RGB12_UNPACKED ||
            pixel_format == IS_CM_BGR12_UNPACKED)
        bits_p_pix = 48;
    else if(pixel_format == IS_CM_RGBA12_UNPACKED ||
            pixel_format == IS_CM_BGRA12_UNPACKED)
        bits_p_pix = 64;
    else
    {
        bits_p_pix = 8;
        warn_msg("can't find right colour mode. setting bits per pixel to 8.",
                ERR_ARG);
    }
}

ids_cam::~ids_cam(void)
{
    im_p.release();

    /* Stop live video mode. */
    err = is_StopLiveVideo(pcam, IS_FORCE_VIDEO_STOP);
    if(err != IS_SUCCESS)
        warn_msg("error forcing live video stop", ERR_ARG);

    /* free image memory. */
    err = is_FreeImageMem(pcam, im_mem, memID);
    if(err != IS_SUCCESS)
        warn_msg("error freeing image memory", ERR_ARG);

    /* close / release camera handler. */
    err = is_ExitCamera(pcam);
    if(err != IS_SUCCESS)
        warn_msg("error releasing camera handler", ERR_ARG);

    #ifndef PYB_NDEBUG
    iprint(stdout, "'%s': memory released\n", __func__);
    #endif
}

void ids_cam::init_Camera(void)
{
    /* Initialise the camera. */
    err = is_InitCamera(&pcam, NULL);
    if(err != IS_SUCCESS)
        err_break = true;
    #ifdef PYB_NDEBUG
    else
        iprint(stdout, "camera initialised successfully\n");
    #endif

    if(caught_Error())
    {
        error_msg("error in the camera initialisation detected.\n" \
                  "is the camera connected?\nexiting.", ERR_ARG);
        exit(EXIT_FAILURE);
    }

    disable_ErrorReport();

    /* Get board and sensor info. */
    err = is_GetSensorInfo(pcam, &s_info);
    if(err != IS_SUCCESS)
    {
        error_msg("error getting sensor info", ERR_ARG);
        err_break = true;
    }

    im_width = im_aoi_width = im_max_width = s_info.nMaxWidth;
    im_height = im_aoi_height = im_max_height = s_info.nMaxHeight;
    color_mod_init = s_info.nColorMode;
    sensorname = s_info.strSensorName;

    identify_CameraAOISettings();

    dpix_size = pix_size * 1e-8 * 1e6; /**< Convert into meters then into um. */
    dpix_size_atm.store(dpix_size, std::memory_order_relaxed);

    err = is_GetCameraInfo(pcam, &b_info);
    if(err != IS_SUCCESS)
    {
        error_msg("error getting board info", ERR_ARG);
        err_break = true;
    }

    get_PixelClock();
    get_PixelClockRange();
    get_Fps();
    get_ExposureTime();
    get_GainBoost();
    print_CameraInfos();

    /* @todo This should be done in a stand-alone colour mode setter: */
    if(pixel_format == IS_CM_MONO8 || pixel_format == IS_CM_SENSOR_RAW8)
        im_p = cv::Mat(cv::Size(im_width, im_height), CV_8UC1, 0.);
    else if(pixel_format == IS_CM_MONO10 ||
            pixel_format == IS_CM_MONO12 ||
            pixel_format == IS_CM_MONO16 ||
            pixel_format == IS_CM_SENSOR_RAW10 ||
            pixel_format == IS_CM_SENSOR_RAW12 ||
            pixel_format == IS_CM_SENSOR_RAW16 ||
            pixel_format == IS_CM_BGR5_PACKED || /* (5 5 5 1) bits */
            pixel_format == IS_CM_BGR565_PACKED) /* (5 6 5) bits */
        im_p = cv::Mat(cv::Size(im_width, im_height), CV_16UC1, 0.);
    else if(pixel_format == IS_CM_UYVY_PACKED ||
            pixel_format == IS_CM_UYVY_MONO_PACKED ||
            pixel_format == IS_CM_UYVY_BAYER_PACKED ||
            pixel_format == IS_CM_CBYCRY_PACKED)
        im_p = cv::Mat(cv::Size(im_width, im_height), CV_8UC2, 0.);
    else if(pixel_format == IS_CM_RGB8_PACKED ||
            pixel_format == IS_CM_BGR8_PACKED ||
            pixel_format == IS_CM_RGB8_PLANAR)
        im_p = cv::Mat(cv::Size(im_width, im_height), CV_8UC3, 0.);
    else if(pixel_format == IS_CM_RGBA8_PACKED ||
            pixel_format == IS_CM_BGRA8_PACKED ||
            pixel_format == IS_CM_RGBY8_PACKED ||
            pixel_format == IS_CM_BGRY8_PACKED ||
            pixel_format == IS_CM_RGB10_PACKED || /* (10 10 10 2) bits */
            pixel_format == IS_CM_BGR10_PACKED) /* (10 10 10 2) bits */
        im_p = cv::Mat(cv::Size(im_width, im_height), CV_8UC4, 0.);
    else if(pixel_format == IS_CM_RGB10_UNPACKED ||
            pixel_format == IS_CM_BGR10_UNPACKED ||
            pixel_format == IS_CM_RGB12_UNPACKED ||
            pixel_format == IS_CM_BGR12_UNPACKED)
        im_p = cv::Mat(cv::Size(im_width, im_height), CV_16UC3, 0.);
    else if(pixel_format == IS_CM_RGBA12_UNPACKED ||
            pixel_format == IS_CM_BGRA12_UNPACKED)
        im_p = cv::Mat(cv::Size(im_width, im_height), CV_16UC4, 0.);
    else
    {
        im_p = cv::Mat(cv::Size(im_width, im_height), CV_8UC1, 0.);
        warn_msg("can't find right colour mode. "
                 "setting Mat format to CV_8UC1.", ERR_ARG);
    }
}

/** \brief Prints infos of the sensor and features to the console.
 *
 * \param void
 * \return void
 *
 */
void ids_cam::print_CameraInfos(void)
{
    #define PR_C "%-23s: "
    iprint(stdout,
           "\n"
           "Camera information\n"
           "------------------\n"
           PR_C "%s\n" \
           PR_C "%s\n" \
           PR_C "%s\n" \
           PR_C "%i\n" \
           PR_C "%i\n" \
           PR_C "%.2g\n" \
           PR_C "%i\n" \
           PR_C "%.2g\n" \
           PR_C "%s\n" \
           PR_C "%u\n" \
           PR_C "%u\n" \
           PR_C "%u\n" \
           PR_C "%u\n" \
           PR_C "%g\n" \
           PR_C "%g\n" \
           PR_C "%g\n" \
           PR_C "%g\n",
           "camera ID", b_info.ID,
           "serial #", b_info.SerNo,
           "date", b_info.Date,
           "pixel size / 1e-8 m", pix_size,
           "width / pixel", im_width,
           "      / um", im_width * dpix_size,
           "height / pixel", im_height,
           "       / um", im_height * dpix_size,
           "initial colour mode", color_mod_init == IS_COLORMODE_MONOCHROME ? ("MONOCHROME") :
                                  color_mod_init == IS_COLORMODE_BAYER ? ("BAYER") :
                                  color_mod_init == IS_COLORMODE_CBYCRY ? ("CBYCRY") :
                                  color_mod_init == IS_COLORMODE_JPEG ? ("JPEG") :
                                  ("INVALID"),
           "pixel clock set/ MHz", pix_clock,
           "            min / MHz", pix_clock_min,
           "            max / MHz", pix_clock_max,
           "            inc / MHz", pix_clock_inc,
           "exposure time set / ms", exp_time,
           "              max / ms", exp_time_max,
           "              min / ms", exp_time_min,
           "              inc / ms", exp_time_inc);
}

void ids_cam::set_ColourMode(void)
{
    err = is_SetColorMode(pcam, pixel_format);
    if(err != IS_SUCCESS)
        error_msg("error setting colour mode", ERR_ARG);
    if(pixel_format != is_SetColorMode(pcam, IS_GET_COLOR_MODE))
        iprint(stderr,
               "error setting colour mode to %u, using %u instead\n",
               pixel_format,
               is_SetColorMode(pcam, IS_GET_COLOR_MODE));
}

/** \brief Set to auto-release camera and memory if camera is disconnected on-the-fly.
 *
 * \param void
 * \return void
 *
 */
void ids_cam::set_ExitMode(void)
{
    err = is_EnableAutoExit(pcam, IS_ENABLE_AUTO_EXIT);
    if(err != IS_SUCCESS)
        warn_msg("error setting auto-exit mode", ERR_ARG);
}

/** \brief Set image mode to DIB = system memory.
 *
 * \param void
 * \return void
 *
 */
void ids_cam::set_Display(void)
{
    err = is_SetDisplayMode(pcam, IS_SET_DM_DIB);
    if(err != IS_SUCCESS)
        error_msg("error setting DIB mode", ERR_ARG);
}

/** \brief Allocate image memory.
 *
 * \param void
 * \return void
 *
 */
void ids_cam::alloc_ImageMem(void)
{
    err = is_AllocImageMem(pcam,
                           im_width,
                           im_height,
                           bits_p_pix,
                           &im_mem,
                           &memID);
    if(err != IS_SUCCESS)
    {
        error_msg("error allocating image memory", ERR_ARG);
        err_break = true;
    }
}

/** \brief Reads out properties of the allocated image memory.
 *
 * \param nx int *res_pt Number of entries in x direction.
 * \param ny int *res_pt Number of entries in x direction.
 * \param bits_per_pix int *res_pt Bits per pixel.
 * \param row_inc int *res_pt Row increment of the image memory.
 * \return void
 *
 * If NULL pointers are supplied, the inquired values are not copied.
 */
void ids_cam::inquire_ImageMem(int *res_pt nx,
                               int *res_pt ny,
                               int *res_pt bits_per_pix,
                               int *res_pt row_inc)
{
    int xo, yo, bppo, pbpo;
    err = is_InquireImageMem(pcam, im_mem, memID, &xo, &yo, &bppo, &pbpo);
    if(err != IS_SUCCESS)
    {
        error_msg("error inquiring image memory", ERR_ARG);
        err_break = true;
    }
    iprint(stdout,
           "\n" \
           "Image memory settings\n" \
           "---------------------\n" \
           "%s: %i\n" \
           "%s: %i\n" \
           "%s: %i\n" \
           "%s: %i\n\n",
           "x / entries   ", xo,
           "y / entries   ", yo,
           "Bits per pixel", bppo,
           "Row increment ", pbpo);
    if(bppo != bits_p_pix)
    {
        error_msg("inquired bits per pixel are different from the user-set value",
                  ERR_ARG);
        err_break = true;
    }
    if(nx)
        *nx = xo;
    if(ny)
        *ny = yo;
    if(bits_per_pix)
        *bits_per_pix = bppo;
    if(row_inc)
        *row_inc = pbpo;
}

/** \brief Activate image memory.
 *
 * \param void
 * \return void
 *
 */
void ids_cam::set_ImageMem(void)
{
    err = is_SetImageMem(pcam, im_mem, memID);
    if(err != IS_SUCCESS)
    {
        error_msg("error activating image memory", ERR_ARG);
        err_break = true;
    }
}

void ids_cam::set_ImageSize(void)
{
    /** Deprecated:
     * err = is_SetImageSize(pcam, width, height);
     */
    IS_RECT rectAOI;
    rectAOI.s32X =
    rectAOI.s32Y = 0;
    rectAOI.s32Width = im_width;
    rectAOI.s32Height = im_height;
    err = is_AOI(pcam, IS_AOI_IMAGE_SET_AOI, (void *)&rectAOI, sizeof(rectAOI));
    if(err != IS_SUCCESS)
        error_msg("error setting image size", ERR_ARG);
}

/** \brief Acquire a single image from the camera.
 *
 * \param img cv::Mat&
 * \return bool
 *
 */
bool ids_cam::get_Image(cv::Mat &img)
{
    err = is_FreezeVideo(pcam, IS_WAIT);

    if(err == IS_SUCCESS)
    {
        /** @todo cv::Mat(int rows, int cols, int type, void* data, size_t step = cv::AUTO_STEP) */
        im_p.data = (uchar *)im_mem;
        im_p.copyTo(img);
        return true;
    }
    handle_IDS_Error(err);
    return false;
}

bool ids_cam::get_Image(void)
{
    err = is_FreezeVideo(pcam, IS_WAIT);
    if(err == IS_SUCCESS) /** @todo cv::Mat(int rows, int cols, int type, void* data, size_t step = AUTO_STEP) */
    {
        im_p.data = (uchar *)im_mem;
        return true;
    }
    handle_IDS_Error(err);
    return false;
}

void ids_cam::handle_IDS_Error(const uchar err)
{
    if(err == IS_OUT_OF_MEMORY)
        error_msg("no memory available", ERR_ARG);
    else if(err == IS_BAD_STRUCTURE_SIZE)
        error_msg("an internal structure has an incorrect size", ERR_ARG);
    else if(err == IS_CANT_OPEN_DEVICE)
        error_msg("no camera connected or initialisation error", ERR_ARG);
    else if(err == IS_CANT_COMMUNICATE_WITH_DRIVER)
        error_msg("driver has not been loaded", ERR_ARG);
    else if(err == IS_TRANSFER_ERROR)
        warn_msg("transfer error", ERR_ARG);
    else if(err == IS_IO_REQUEST_FAILED)
        error_msg("an IO request from the ueye driver failed", ERR_ARG);
    else if(err == IS_CAPTURE_RUNNING)
        warn_msg("capture operation in progress must be " \
                 "terminated before starting another one",
                 ERR_ARG);
    else
        warn_msg("unspecified error / warning during image capture", ERR_ARG);
}

cv::Mat ids_cam::get_Mat(void)
{
    return im_p.clone();
}

int ids_cam::get_Width(void)
{
    return im_width;
}

int ids_cam::get_Height(void)
{
    return im_height;
}

bool ids_cam::caught_Error(void)
{
    if(err_break)
        return true;
    else
        return false;
}

/** \brief Retrieves the approximate bandwidth in MByte per second.
 *
 * \param void
 * \return void
 *
 */
void ids_cam::get_UsedBandwidth(void)
{
    used_bandwidth = is_GetUsedBandwidth(pcam);
}

/** \brief Retrieves the pixel clock in arbitrary units.
 *
 * \param void
 * \return void
 *
 */
void ids_cam::get_PixelClock(void)
{
    err = is_PixelClock(pcam,
                        IS_PIXELCLOCK_CMD_GET,
                        (void *)&pix_clock,
                        sizeof(pix_clock));
    if(err != IS_SUCCESS)
    {
        warn_msg("error getting pixel clock", ERR_ARG);
        err_break = true;
    }
}

void ids_cam::get_PixelClockRange(void)
{
    unsigned int nRange[3] = {0, 0, 0};
    err = is_PixelClock(pcam,
                        IS_PIXELCLOCK_CMD_GET_RANGE,
                        (void*)nRange,
                        sizeof(nRange));
    if(err != IS_SUCCESS)
    {
        warn_msg("error getting pixel clock range", ERR_ARG);
        err_break = false;
    }
    pix_clock_min = nRange[0];
    pix_clock_max = nRange[1];
    pix_clock_inc = nRange[2];
}

/** \brief Sets the pixel clock in MHz.
 *
 * \param pc const int Pixel clock to set.
 * \return void
 *
 */
void ids_cam::set_PixelClock(const int pc)
{
    err = is_PixelClock(pcam,
                        IS_PIXELCLOCK_CMD_SET,
                        (void*)&pc, sizeof(pc));
    if(err != IS_SUCCESS)
    {
        warn_msg("error setting pixel clock", ERR_ARG);
        err_break = false;
    }
}

void ids_cam::set_PixelClock(const std::string clock)
{
    if(!clock.compare("min") || !clock.compare("Min"))
    {
        err = is_PixelClock(pcam,
                    IS_PIXELCLOCK_CMD_SET,
                    (void*)&pix_clock_min, sizeof(pix_clock_min));
        if(err != IS_SUCCESS)
        {
            warn_msg("error setting pixel clock", ERR_ARG);
            err_break = false;
        }
    }
    else if(!clock.compare("max") || !clock.compare("Max"))
    {
            err = is_PixelClock(pcam,
                IS_PIXELCLOCK_CMD_SET,
                (void*)&pix_clock_max, sizeof(pix_clock_max));
        if(err != IS_SUCCESS)
        {
            warn_msg("error setting pixel clock", ERR_ARG);
            err_break = false;
        }
    }
    else
    {
        warn_msg("wrong specifier, accepts only min or max", ERR_ARG);
    }
}

/** \brief Retrieve frame rate.
 *
 * \param void
 * \return void
 *
 */
void ids_cam::get_Fps(void)
{
    err = is_GetFramesPerSecond(pcam, &fps);
    if(err != IS_SUCCESS)
    {
        warn_msg("error getting frame rate", ERR_ARG);
        err_break = true;
    }
}

/** \brief Return the pixel pixel pitch in um.
 *
 * \param void
 * \return double Pixel pitch in um
 *
 */
double ids_cam::get_PixelPitch(void)
{
    return dpix_size;
}

void ids_cam::get_ExposureTime(void)
{
    double rng[3],
           time;
    uint32_t ncaps;
    err = is_Exposure(pcam,
                      IS_EXPOSURE_CMD_GET_CAPS,
                      (void *)&ncaps,
                      sizeof(ncaps));
    if(err != IS_SUCCESS)
    {
        error_msg("error getting exposure function modes", ERR_ARG);
        err_break = true;
    }
    else
    {
        if(ncaps & IS_EXPOSURE_CAP_EXPOSURE)
        {
            iprint(stdout, "exposure time setting supported\n");
            cntrl_exp_time = true;
        }
        else
            cntrl_exp_time = false;
        if(ncaps & IS_EXPOSURE_CAP_FINE_INCREMENT)
        {
            iprint(stdout, "exposure time fine increment supported\n");
            supp_fine_inc_exp_time = true;
        }
        else
            supp_fine_inc_exp_time = false;
        if(ncaps & IS_EXPOSURE_CAP_LONG_EXPOSURE)
            iprint(stdout, "long exposure time supported\n");
    }

    err = is_Exposure(pcam,
                      IS_EXPOSURE_CMD_GET_EXPOSURE,
                      (void *)&time,
                      sizeof(time));
    if(err != IS_SUCCESS)
    {
        error_msg("error getting exposure time", ERR_ARG);
        err_break = true;
    }
    exp_time = time;
    err = is_Exposure(pcam,
                      IS_EXPOSURE_CMD_GET_EXPOSURE_RANGE,
                      (void *)rng,
                      sizeof(rng));
    if(err != IS_SUCCESS)
    {
        error_msg("error getting exposure time range", ERR_ARG);
        err_break = true;
    }
    exp_time_min = rng[0];
    exp_time_max = rng[1];
    exp_time_inc = rng[2];

    set_ExposureTimeAtomic(exp_time);
    exp_time_min_atm.store(exp_time_min, std::memory_order_relaxed);
    exp_time_max_atm.store(exp_time_max, std::memory_order_relaxed);
    exp_time_inc_atm.store(exp_time_inc, std::memory_order_relaxed);
}

void ids_cam::set_ExposureTime(double time)
{
    if(time > exp_time_max)
        time = exp_time_max;
    else if(time < exp_time_min)
        time = exp_time_min;

    err = is_Exposure(pcam,
                      IS_EXPOSURE_CMD_SET_EXPOSURE,
                      (void *)&time,
                      sizeof(time));
    if(err != IS_SUCCESS)
    {
        error_msg("error setting exposure time", ERR_ARG);
        err_break = true;
    }
    else
    {
        #ifndef PYB_NDEBUG
        iprint(stdout, "exposure time set to %g ms\n", time);
        #endif
        exp_time = time;
        set_ExposureTimeAtomic(exp_time);
    }
}

void ids_cam::exchange_ExposureTimeAtomic(void)
{
    double time = exp_time_atm.load(std::memory_order_acquire);
    if(time != exp_time)
    {
        if(time > exp_time_max)
            time = exp_time_max;
        else if(time < exp_time_min)
            time = exp_time_min;

        err = is_Exposure(pcam,
                          IS_EXPOSURE_CMD_SET_EXPOSURE,
                          (void *)&time,
                          sizeof(time));
        if(err != IS_SUCCESS)
        {
            error_msg("error setting exposure time", ERR_ARG);
            err_break = true;
        }
        else
        {
            #ifndef PYB_NDEBUG
            iprint(stdout, "exposure time set to %g ms\n", time);
            #endif
            exp_time = time;
            exp_time_atm.store(exp_time, std::memory_order_release);
        }
    }
}

void ids_cam::set_ExposureTimeAtomic(double time)
{
    exp_time_atm.store(time, std::memory_order_relaxed);
}

void ids_cam::get_ExposureTimesAtomic(double *res_pt time,
                                      double *res_pt min_time,
                                      double *res_pt max_time,
                                      double *res_pt inc_time)
{
    *time = exp_time_atm.load(std::memory_order_acquire);
    *min_time = exp_time_min_atm.load(std::memory_order_acquire);
    *max_time = exp_time_max_atm.load(std::memory_order_acquire);
    *inc_time = exp_time_inc_atm.load(std::memory_order_acquire);
}

void ids_cam::get_CameraStatus(void)
{
    ulong status;
    status = is_CameraStatus(pcam,
                             IS_LAST_CAPTURE_ERROR,
                             IS_GET_STATUS);
    switch(status)
    {
    case IS_BAD_STRUCTURE_SIZE:
        error_msg("an internal structure has an incorrect size", ERR_ARG);
        break;
    case IS_CANT_COMMUNICATE_WITH_DRIVER:
        error_msg("no driver loaded, communication with camera failed", ERR_ARG);
        break;
    case IS_CANT_OPEN_DEVICE:
        error_msg("no camera connected or initialization error", ERR_ARG);
        break;
    case IS_INVALID_CAMERA_HANDLE:
        error_msg("invalid camera handle", ERR_ARG);
        break;
    case IS_OUT_OF_MEMORY:
        error_msg("no memory could be allocated", ERR_ARG);
        break;
    case IS_TIMED_OUT:
        warn_msg("an image capturing process could not be terminated within the allowable period", ERR_ARG);
        break;
    }
}

/** \brief Sets the error event logging from IDS driver to off.
 *
 * \param void
 * \return void
 *
 * Note that errors can still be handled, only the dialogue box generated by
 * the IDS driver (on Win systems) is disabled.
 */
void ids_cam::disable_ErrorReport(void)
{
    err = is_SetErrorReport(pcam, IS_DISABLE_ERR_REP);
    if(err != IS_SUCCESS)
    {
        error_msg("error disabling error report", ERR_ARG);
        err_break = true;
    }
}

void ids_cam::get_GainBoost(void)
{
    err = is_SetGainBoost(pcam, IS_GET_SUPPORTED_GAINBOOST);
    if(err == IS_SET_GAINBOOST_ON)
    {
        err = is_SetGainBoost(pcam, IS_GET_GAINBOOST);
        if(err == IS_SET_GAINBOOST_ON)
            iprint(stdout,
                   "analogue hardware gain boost feature available and switched on\n");
        else
            iprint(stdout,
                   "analogue hardware gain boost feature available and switched off\n");
    }
    else if(err == IS_SET_GAINBOOST_OFF)
        iprint(stdout,
               "analogue hardware gain boost feature not available\n");
    else
        iprint(stdout,
               "error detecting analogue hardware gain boost feature\n");
}

void ids_cam::TrackbarHandlerExposure(int i)
{
    const double out_min = 0., out_max = 100.;
    double res = map_Linear((double)i,
                            out_min,
                            out_max,
                            exp_time_min,
                            exp_time_max);
    (*this).set_ExposureTime(res);
}

void ids_cam::cast_static_SetTrackbarHandlerExposure(int i, void *ptr)
{
    ids_cam *cam = static_cast<ids_cam *>(ptr);
    (*cam).TrackbarHandlerExposure(i);
}

void ids_cam::create_TrackbarExposure(void)
{
    const std::string trck_name = "Exposure";
    const double out_min = 0., out_max = 100.;
    double res = map_Linear(exp_time,
                            exp_time_min,
                            exp_time_max,
                            out_min,
                            out_max);
    static int setting = res; /* Must be static, as its memory address is stored
    in the cv function. */
    assert(setting <= 100);
    /**
     * 3 parameters are:
     * The address of the variable that is changing when the trackbar is moved,
     * the maximum value the trackbar can move,
     * and the function that is called whenever the trackbar is moved.
     */
    cv::createTrackbar(trck_name,
                       infotbar_win_name,
                       &setting,
                       (int)out_max,
                       ids_cam::cast_static_SetTrackbarHandlerExposure,
                       this);
}

void ids_cam::draw_CameraInfo(void)
{
    infotbar_win_mat.setTo(0);
    const uint16_t lw = 1;
    double fscl = .45,
           sx, sy;
    const int font = cv::FONT_HERSHEY_SIMPLEX;
    const cv::Scalar_<double> clr_txt(0., 255., 0.);
    static std::string info(128, '\0');
    #define putText_ARGS cv::Point2d(sx, sy), font, fscl, clr_txt, lw, cv::LINE_AA

    sx = 10.;
    sy = infotbar_win_mat.rows - 10.;
    get_Fps();
    info = "frame rate: " + convert_Double2Str(fps, 1) + " 1 / s";
    cv::putText(infotbar_win_mat, info, putText_ARGS);
    sy -= 20.;
    info = "exposure time: " + convert_Double2Str(exp_time, 3) + " ms";
    cv::putText(infotbar_win_mat, info, putText_ARGS);
    sy -= 20.;
    info = "(start / end) width: (" +
           convert_Int2Str(im_aoi_width_start) + ", " +
           convert_Int2Str(im_aoi_width) + ") pixel";
    cv::putText(infotbar_win_mat, info, putText_ARGS);
    sy -= 20.;
    info = "(start / end) height: (" +
           convert_Int2Str(im_aoi_height_start) + ", " +
           convert_Int2Str(im_aoi_height) + ") pixel";
    cv::putText(infotbar_win_mat, info, putText_ARGS);
    sy -= 20.;
    info = "sensor area: (" +
           convert_Int2Str(sensor_aa_width) + " * " +
           convert_Int2Str(sensor_aa_height) + ") um^2";
    cv::putText(infotbar_win_mat, info, putText_ARGS);
    sy -= 20.;
    info = "camera model: " + sensorname;
    cv::putText(infotbar_win_mat, info, putText_ARGS);
    #undef putText_ARGS
}

std::string ids_cam::get_CameraInfo(void)
{
    std::string out;
    out = "Camera model: " + sensorname +
          "\nSensor area: (" +
          convert_Int2Str(sensor_aa_width) + " * " +
          convert_Int2Str(sensor_aa_height) + ") um^2\n" +
          "Pixel count: " +
          convert_Int2Str(im_aoi_width) + " * " +
          convert_Int2Str(im_aoi_height) +
          "\nPixel pitch: " + convert_Double2Str(dpix_size) + " um / pix";
    return out;
}

/** \brief Identifies the Area Of Interest (AOI) options of a camera.
 *
 * \param void
 * \return void
 *
 * The options vary from model to model. We identify the camera / sensor and set
 * the minimal image size and the increment manually. The data can be read from
 * the manual of each sensor.
 */
void ids_cam::identify_CameraAOISettings(void)
{
    if(!sensorname.compare("DCC1240x") ||
       !sensorname.compare("DCC1240M") ||
       !sensorname.compare("DCC1240C") ||
       !sensorname.compare("DCC1240N") ||
       !sensorname.compare("DCC3240x") ||
       !sensorname.compare("DCC3240M") ||
       !sensorname.compare("DCC3240C") ||
       !sensorname.compare("DCC3240N"))
    {
        /* Sensor is
        e2v EV76C560ABT (monochrome) or
        e2v EV76C560ACT (colour) or
        e2v EV76C661ABT (NIR). */
        im_min_width = 16;
        im_inc_width = 4;
        im_min_height = 4;
        im_inc_height = 2;
        sensor_aa_width = 6784;
        sensor_aa_height = 5427;
        if(!pix_size)
            pix_size = 530;
    }
    else if(!sensorname.compare("DCC1545M"))
    {
        /* Sensor is Aptina MT9M001. */
        im_min_width = 32;
        im_inc_width = 4;
        im_min_height = 4;
        im_inc_height = 2;
        sensor_aa_width = 6656;
        sensor_aa_height = 5325;
        if(!pix_size)
            pix_size = 520;
    }
    else if(!sensorname.compare("DCC1645C"))
    {
        /* Sensor is Aptina MT9M131. */
        im_min_width = 32;
        im_inc_width = 4;
        im_min_height = 4;
        im_inc_height = 2;
        sensor_aa_width = 4608; /* Exact sensitive area in um. */
        sensor_aa_height = 3686;
        if(!pix_size)
            pix_size = 360;
    }
    else if(!sensorname.compare("DCU223x") ||
            !sensorname.compare("DCU223C"))
    {
        /* Sensor is Sony ICX204AK. */
        im_min_width = 16;
        im_inc_width = 4;
        im_min_height = 120;
        im_inc_height = 2;
        sensor_aa_width = 4762;
        sensor_aa_height = 3571;
        if(!pix_size)
            pix_size = 465;
    }
    else if(!sensorname.compare("DCU223M"))
    {
        /* Sensor is Sony ICX204AL. */
        im_min_width = 16;
        im_inc_width = 4;
        im_min_height = 120;
        im_inc_height = 1;
        sensor_aa_width = 4762;
        sensor_aa_height = 3571;
        if(!pix_size)
            pix_size = 465;
    }
    else if(!sensorname.compare("DCU224C"))
    {
        /* Sensor is Sony ICX205AK. */
        im_min_width = 16;
        im_inc_width = 4;
        im_min_height = 120;
        im_inc_height = 2;
        sensor_aa_width = 5952;
        sensor_aa_height = 4762;
        if(!pix_size)
            pix_size = 465;
    }
    else if(!sensorname.compare("DCU224M"))
    {
        /* Sensor is Sony ICX205AL. */
        im_min_width = 16;
        im_inc_width = 4;
        im_min_height = 120;
        im_inc_height = 1;
        sensor_aa_width = 5952;
        sensor_aa_height = 4762;
        if(!pix_size)
            pix_size = 465;
    }
    else if(!sensorname.compare("UI125xML-C"))
    {
        /* Sensor is E2V EV76C570ACT. */
        im_min_width = 16;
        im_inc_width = 4;
        im_min_height = 4;
        im_inc_height = 2;
        sensor_aa_width = 7200;
        sensor_aa_height = 5400;
        if(!pix_size)
            pix_size = 450;
    }
    else if(!sensorname.compare("UI154xLE-M"))
    {
        /* Sensor is ON Semiconductor MT9M001STM. */
        im_min_width = 32;
        im_inc_width = 4;
        im_min_height = 4;
        im_inc_height = 2;
        sensor_aa_width = 6656;
        sensor_aa_height = 5325;
        if(!pix_size)
            pix_size = 520;
    }
    else
    {
        std::string outs = "error identifying the sensor name (" +
                            sensorname +
                            "). setting AOI is dangerous!";
        error_msg(outs.c_str(), ERR_ARG);
    }
}

void ids_cam::set_CameraInfoWindowName(const std::string &name)
{
    infotbar_win_name = name;
}

std::string ids_cam::get_CameraInfoWindowName(void)
{
    return infotbar_win_name;
}

void ids_cam::show_CameraTrackbars(void)
{
    cv::imshow(infotbar_win_name, infotbar_win_mat);
}
