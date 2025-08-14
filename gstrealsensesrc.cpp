/* GStreamer RealSense is a set of plugins to acquire frames from 
 * Intel RealSense cameras into GStreamer pipeline.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-realsensesrc
 *
 * Source element for Intel RealSense camera. This source takes the 
 * frame_set from the RealSense SDK and multiplexes it into a single buffer
 * that is pushed out on the source pad. Downstream elements may receive this buffer
 * and demux it themselves (use RSMux::demux) or use the rsdemux element to split
 * the color and depth into separate buffers.
 *
 * Example launch line
 * |[
 * gst-launch-1.0 -v -m realsensesrc ! videoconvert ! autovideosink
 * ]|
 * 
 * The example pipeline will display muxed data, so the depth and IMU data 
 * will not be displayed correctly. See rsdemux element to split the sources
 * into seperate streams.
 * 
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include "gstrealsensesrc.h"
#include <cmath>
#include <fstream>
#include <vector>
#include <tuple>

GST_DEBUG_CATEGORY_STATIC (gst_realsense_src_debug);
#define GST_CAT_DEFAULT gst_realsense_src_debug

enum
{
  PROP_0,
  PROP_ALIGN,
  PROP_COLOR_WIDTH,
  PROP_COLOR_HEIGHT,
  PROP_COLOR_FPS,
  PROP_DEPTH_WIDTH,
  PROP_DEPTH_HEIGHT,
  PROP_DEPTH_FPS,
  PROP_PRESET_FILE
};

/* the capabilities of the inputs and outputs.
 */
#define RSS_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) "," \
  "multiview-mode = { mono, left, right }"                              \
  ";" \
  "video/x-bayer, format=(string) { bggr, rggb, grbg, gbrg }, "        \
  "width = " GST_VIDEO_SIZE_RANGE ", "                                 \
  "height = " GST_VIDEO_SIZE_RANGE ", "                                \
  "framerate = " GST_VIDEO_FPS_RANGE ", "                              \
  "multiview-mode = { mono, left, right }"



#define gst_realsense_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstRealsenseSrc, gst_realsense_src, 
    GST_TYPE_PUSH_SRC,
    GST_DEBUG_CATEGORY_INIT (gst_realsense_src_debug, "realsensesrc",
      0, "Template realsensesrc"));
      
static void gst_realsense_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_realsense_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_realsense_src_dispose(GObject *object);
static void gst_realsense_src_finalize(GObject *object);
static GstFlowReturn gst_realsense_src_create (GstPushSrc * src, GstBuffer ** buf);

static gboolean gst_realsense_src_start (GstBaseSrc * basesrc);
static gboolean gst_realsense_src_stop (GstBaseSrc * basesrc);
static GstCaps *gst_realsense_src_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_realsense_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_realsense_src_unlock (GstBaseSrc * basesrc);
static gboolean gst_realsense_src_unlock_stop (GstBaseSrc * basesrc);


static GstStaticPadTemplate gst_realsense_src_pad_template =
GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "video/x-raw, "
        "format = (string) RGB, "
        "width = (int) [1, MAX], "
        "height = (int) [1, MAX], "
        "framerate = (fraction) [0/1, MAX]"
    )
);

/* initialize the realsensesrc's class */
static void
gst_realsense_src_class_init (GstRealsenseSrcClass * klass)
{

  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS(klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS(klass);

  gobject_class->set_property = gst_realsense_src_set_property;
  gobject_class->get_property = gst_realsense_src_get_property;
  gobject_class->dispose = gst_realsense_src_dispose;
  gobject_class->finalize = gst_realsense_src_finalize;

  gst_element_class_set_details_simple(gstelement_class,
    "RealsenseSrc",
    "Source/Video/Sensors",
    "Source element for Intel RealSense multiplexed video, depth and IMU data",
    "ravi kalmodia>>");

  //gst_element_class_add_pad_template(gstelement_class,gst_static_pad_template_get(&gst_realsense_src_template));
  gst_element_class_add_static_pad_template(gstelement_class,
                                          &gst_realsense_src_pad_template);

  // gst_element_class_add_static_pad_template (gstelement_class, &src_factory);

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR(gst_realsense_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR(gst_realsense_src_stop);
  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR(gst_realsense_src_get_caps);
  gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR(gst_realsense_src_set_caps);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_realsense_src_unlock);
  gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_realsense_src_unlock_stop);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR(gst_realsense_src_create);

  // Properties
  // see Pattern property in VideoTestSrc for usage of enum propert
  g_object_class_install_property (gobject_class, PROP_ALIGN,
    g_param_spec_int (
      "align",
      "Alignment",
      "Alignment between Color and Depth sensors. Valid values: 0=None, 1=Color, 2=Depth. Default: None.",
      Align::None, Align::Depth, 0,
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  
  // Register new properties for color and depth stream configuration
  g_object_class_install_property (gobject_class, PROP_COLOR_WIDTH,
    g_param_spec_int (
      "color-width",
      "Color Width",
      "Width of the color stream. Must be one of the supported RealSense resolutions. Default: 1280.",
      1, 4096, 1280,
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLOR_HEIGHT,
    g_param_spec_int (
      "color-height",
      "Color Height",
      "Height of the color stream. Must be one of the supported RealSense resolutions. Default: 720.",
      1, 2160, 720,
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLOR_FPS,
    g_param_spec_int (
      "color-fps",
      "Color FPS",
      "Frame rate of the color stream. Must be one of the supported RealSense values. Default: 30.",
      1, 120, 30,
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DEPTH_WIDTH,
    g_param_spec_int (
      "depth-width",
      "Depth Width",
      "Width of the depth stream. Must be one of the supported RealSense resolutions. Default: 640.",
      1, 2048, 640,
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DEPTH_HEIGHT,
    g_param_spec_int (
      "depth-height",
      "Depth Height",
      "Height of the depth stream. Must be one of the supported RealSense resolutions. Default: 480.",
      1, 1536, 480,
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DEPTH_FPS,
    g_param_spec_int (
      "depth-fps",
      "Depth FPS",
      "Frame rate of the depth stream. Must be one of the supported RealSense values. Default: 30.",
      1, 120, 30,
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_PRESET_FILE,
    g_param_spec_string (
      "preset-file",
      "Preset File Path",
      "Path to a RealSense JSON preset file to configure the camera in advanced mode. "
      "If set, the file will be loaded at pipeline start (for D435i only). "
      "If not set or empty, the camera will use its default configuration. "
      "This property is optional and only needed for custom tuning.",
      NULL,
      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_realsense_src_reset(GstRealsenseSrc *src) {
  if(src->rs_pipeline != nullptr)
    src->rs_pipeline->stop();

  src->out_framesize = 0;
  src->frame_count = 0;

  if (src->caps) {
      gst_caps_unref(src->caps);
      src->caps = NULL;
  }
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_realsense_src_init (GstRealsenseSrc * src)
{
  /* set source as live (no preroll) */
  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);

  /* override default of BYTES to operate in time mode */
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  src->color_width = 1280;
  src->color_height = 720;
  src->color_fps = 30;
  src->depth_width = 640;
  src->depth_height = 480;
  src->depth_fps = 30;
  src->align = Align::Color;
  src->preset_file = NULL;
  src->stop_requested = FALSE;
  src->caps = NULL;
  gst_realsense_src_reset(src);

}

// Supported (width, height, fps) combinations for color and depth
static const std::vector<std::tuple<int, int, int>> valid_color_modes = {
    {1920, 1080, 6}, {1920, 1080, 15}, {1920, 1080, 30},
    {1280, 720, 6}, {1280, 720, 15}, {1280, 720, 30},
    {960, 540, 6}, {960, 540, 15}, {960, 540, 30}, {960, 540, 60},
    {848, 480, 6}, {848, 480, 15}, {848, 480, 30}, {848, 480, 60},
    {640, 480, 6}, {640, 480, 15}, {640, 480, 30}, {640, 480, 60},
    {640, 360, 6}, {640, 360, 15}, {640, 360, 30}, {640, 360, 60},
    {424, 240, 6}, {424, 240, 15}, {424, 240, 30}, {424, 240, 60},
    {320, 240, 6}, {320, 240, 30}, {320, 240, 60},
    {320, 180, 6}, {320, 180, 30}, {320, 180, 60}
};
static const std::vector<std::tuple<int, int, int>> valid_depth_modes = {
    {1280, 720, 6}, {1280, 720, 15}, {1280, 720, 30},
    {848, 480, 6}, {848, 480, 15}, {848, 480, 30}, {848, 480, 60}, {848, 480, 90},
    {640, 480, 6}, {640, 480, 15}, {640, 480, 30}, {640, 480, 60}, {640, 480, 90},
    {640, 360, 6}, {640, 360, 15}, {640, 360, 30}, {640, 360, 60}, {640, 360, 90},
    {480, 270, 6}, {480, 270, 15}, {480, 270, 30}, {480, 270, 60}, {480, 270, 90},
    {424, 240, 6}, {424, 240, 15}, {424, 240, 30}, {424, 240, 60}, {424, 240, 90}
};

static bool is_valid_mode(const std::vector<std::tuple<int, int, int>>& modes, int w, int h, int fps) {
    for (const auto& t : modes) {
        if (std::get<0>(t) == w && std::get<1>(t) == h && std::get<2>(t) == fps)
            return true;
    }
    return false;
}

static void
gst_realsense_src_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRealsenseSrc *src = GST_REALSENSESRC (object);

  switch (prop_id) 
  {
    case PROP_ALIGN:
      src->align = static_cast<Align>(g_value_get_int(value));
      break;
    case PROP_COLOR_WIDTH:
      src->color_width = g_value_get_int(value);
      break;
    case PROP_COLOR_HEIGHT:
      src->color_height = g_value_get_int(value);
      break;
    case PROP_COLOR_FPS:
      src->color_fps = g_value_get_int(value);
      break;
    case PROP_DEPTH_WIDTH:
      src->depth_width = g_value_get_int(value);
      break;
    case PROP_DEPTH_HEIGHT:
      src->depth_height = g_value_get_int(value);
      break;
    case PROP_DEPTH_FPS:
      src->depth_fps = g_value_get_int(value);
      break;
    case PROP_PRESET_FILE:
      if (src->preset_file)
        g_free(src->preset_file);
      src->preset_file = g_value_dup_string(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  // Validate color mode
  if (src->color_width > 0 && src->color_height > 0 && src->color_fps > 0){
    if (!is_valid_mode(valid_color_modes, src->color_width, src->color_height, src->color_fps)) {
      GST_ELEMENT_WARNING(src, RESOURCE, SETTINGS,
        ("Invalid color mode: %dx%d@%d. Reverting to default valid value. 1280*720*30 ", src->color_width, src->color_height, src->color_fps), (NULL));
      src->color_width = 1280;
      src->color_height = 720;
      src->color_fps = 30;
    }
  }
  // Validate depth mode
  if (src->depth_width > 0 && src->depth_height > 0 && src->depth_fps > 0) {
    if (!is_valid_mode(valid_depth_modes, src->depth_width, src->depth_height, src->depth_fps)) {
      GST_ELEMENT_WARNING(src, RESOURCE, SETTINGS,
        ("Invalid depth mode: %dx%d@%d. Reverting to default valid value. 640*480*30 ", src->depth_width, src->depth_height, src->depth_fps), (NULL));
      src->depth_width = 640;
      src->depth_height = 480;
      src->depth_fps = 30;
    }
  }
}

static void
gst_realsense_src_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRealsenseSrc *src = GST_REALSENSESRC (object);
  
  switch (prop_id) {
    case PROP_ALIGN:
      g_value_set_int(value, src->align);
      break;
    case PROP_COLOR_WIDTH:
      g_value_set_int(value, src->color_width);
      break;
    case PROP_COLOR_HEIGHT:
      g_value_set_int(value, src->color_height);
      break;
    case PROP_COLOR_FPS:
      g_value_set_int(value, src->color_fps);
      break;
    case PROP_DEPTH_WIDTH:
      g_value_set_int(value, src->depth_width);
      break;
    case PROP_DEPTH_HEIGHT:
      g_value_set_int(value, src->depth_height);
      break;
    case PROP_DEPTH_FPS:
      g_value_set_int(value, src->depth_fps);
      break;
    case PROP_PRESET_FILE:
      g_value_set_string(value, src->preset_file);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_realsense_src_unlock (GstBaseSrc * basesrc)
{
  GstRealsenseSrc *src = GST_REALSENSESRC (basesrc);

  GST_LOG_OBJECT (src, "unlock");

  src->stop_requested = TRUE;

  return TRUE;
}

static gboolean
gst_realsense_src_unlock_stop (GstBaseSrc * basesrc)
{
  GstRealsenseSrc *src = GST_REALSENSESRC (basesrc);

  GST_LOG_OBJECT (src, "unlock_stop");

  src->stop_requested = FALSE;

  return TRUE;
}

static gboolean
gst_realsense_src_stop (GstBaseSrc * basesrc)
{
  auto *src = GST_REALSENSESRC (basesrc);
   GST_TRACE_OBJECT(src, "gst_realsense_src_stop");
  gst_realsense_src_reset(src); 
  return TRUE;
}


static GstCaps *
gst_realsense_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstRealsenseSrc *src = GST_REALSENSESRC (bsrc);
  GstCaps *caps;

  if (src->rs_pipeline == nullptr) {
    caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (src));
  } else {
    caps = gst_caps_copy (src->caps);
  }

  GST_DEBUG_OBJECT (src, "The caps before filtering are %" GST_PTR_FORMAT,
      caps);

  if (filter && caps) {
    GstCaps *tmp = gst_caps_intersect (caps, filter);
    gst_caps_unref (caps);
    caps = tmp;
  }

  GST_DEBUG_OBJECT (src, "The caps after filtering are %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_realsense_src_set_caps(GstBaseSrc *bsrc, GstCaps *caps)
{
  GstRealsenseSrc *src = GST_REALSENSESRC(bsrc);
  GstVideoInfo vinfo;

  GST_DEBUG_OBJECT(src, "The caps being set are %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps(&vinfo, caps)) {
    GST_ERROR_OBJECT(src, "Failed to parse video info from caps");
    return FALSE;
  }

  if (GST_VIDEO_INFO_FORMAT(&vinfo) != GST_VIDEO_FORMAT_RGB) {
    GST_ERROR_OBJECT(src, "Unsupported video format: %s",
                     gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&vinfo)));
    return FALSE;
  }

  src->gst_stride = GST_VIDEO_INFO_COMP_STRIDE(&vinfo, 0);

  return TRUE;
}


void gst_realsense_src_dispose(GObject *object) {
    GstRealsenseSrc *src;

    g_return_if_fail(GST_IS_REALSENSESRC(object));
    src = GST_REALSENSESRC(object);

    GST_TRACE_OBJECT(src, "gst_realsense_src_dispose");

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS(gst_realsense_src_parent_class)->dispose(object);
}

void gst_realsense_src_finalize(GObject *object) {
    GstRealsenseSrc *src;

    g_return_if_fail(GST_IS_REALSENSESRC(object));
    src = GST_REALSENSESRC(object);

    GST_TRACE_OBJECT(src, "gst_realsense_src_finalize");

    /* clean up object here */
    if (src->caps) {
        gst_caps_unref(src->caps);
        src->caps = NULL;
    }

    G_OBJECT_CLASS(gst_realsense_src_parent_class)->finalize(object);
}


static gboolean gst_realsense_src_calculate_caps(GstRealsenseSrc *src) {
    GST_TRACE_OBJECT(src, "gst_realsense_src_calculate_caps");

    guint32 width = 0, height = 0;
    GstVideoInfo vinfo;

    try {
        // Fetch RealSense frame
        auto frame_set = src->rs_pipeline->wait_for_frames();
        if (src->aligner)
            frame_set = src->aligner->process(frame_set);

        auto cframe = frame_set.get_color_frame();
        width = cframe.get_width();
        height = cframe.get_height() * 2; // top (color) + bottom (depth encoded)

        // Set RGB format for CPU buffer
        GstVideoFormat fmt = GST_VIDEO_FORMAT_RGB;

        gst_video_info_init(&vinfo);
        gst_video_info_set_format(&vinfo, fmt, width, height);
        vinfo.fps_n = 30;
        vinfo.fps_d = 1;

        if (src->caps)
            gst_caps_unref(src->caps);

        src->caps = gst_video_info_to_caps(&vinfo);

        src->out_framesize = GST_VIDEO_INFO_SIZE(&vinfo);
        gst_base_src_set_blocksize(GST_BASE_SRC(src), src->out_framesize);
        gst_base_src_set_caps(GST_BASE_SRC(src), src->caps);

        GST_DEBUG_OBJECT(src, "Calculated caps: %" GST_PTR_FORMAT, src->caps);
        return TRUE;

    } catch (const rs2::error &e) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
            ("RealSense error during caps calculation: %s (%s)",
             e.get_failed_function().c_str(),
             e.get_failed_args().c_str()),
            (NULL));
        return FALSE;
    }
}


static GstFlowReturn gst_realsense_src_create(GstPushSrc* psrc, GstBuffer** buf) {
    GstRealsenseSrc* src = GST_REALSENSESRC(psrc);
    GST_TRACE_OBJECT(src, "gst_realsense_src_create");
    GST_LOG_OBJECT (src, "create");

    GST_CAT_DEBUG(gst_realsense_src_debug, "creating frame buffer");

    GstMapInfo minfo;
    GstClock *clock;
    GstClockTime clock_time;
    static int temp_ugly_buf_index = 0;

    try {
      auto frame_set = src->rs_pipeline->wait_for_frames();
      if(src->aligner != nullptr)
        frame_set = src->aligner->process(frame_set);
      
      GST_CAT_DEBUG(gst_realsense_src_debug, "received frame from realsense");
    // ----> Clock update
      clock = gst_element_get_clock(GST_ELEMENT(src));
      clock_time = gst_clock_get_time(clock);
      gst_object_unref(clock);
      // <---- Clock update

      /* create GstBuffer then release */
      *buf = gst_buffer_new_and_alloc(src->out_framesize);
      if (!*buf) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("failed to allocate buffer"), (NULL));
        throw std::runtime_error("failed to allocate buffer");
      }
      if (FALSE == gst_buffer_map(*buf, &minfo, GST_MAP_WRITE)) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("Failed to map buffer for writing"), (NULL));
        return GST_FLOW_ERROR;
      }

      const auto& cframe = frame_set.get_color_frame();
      const auto& depth = frame_set.get_depth_frame();

      const auto color_data = static_cast<const guint8*>(cframe.get_data());
      const auto depth_data = reinterpret_cast<const uint16_t*>(depth.get_data());

      int width = cframe.get_width();
      int height = cframe.get_height();
      int num_pixels = width * height;

      guint8* top_half = minfo.data;
      guint8* bottom_half = minfo.data + minfo.size / 2;

      // ----> Top half: RGB color
      memcpy(top_half, color_data, minfo.size / 2);

      // ----> Bottom half: Depth encoded to RGB
      for (int i = 0; i < num_pixels; ++i) {
                uint16_t depth_val = depth_data[i];
          guint8* pixel = bottom_half + i * 3;

          if (depth_val < 2560) {
              pixel[0] = depth_val % 10;         // R
              pixel[1] = depth_val / 10;         // G
              pixel[2] = depth_val % 10;         // B
          } else {
              pixel[0] = pixel[1] = pixel[2] = 0;
          }
      }

    // ----> Timestamp meta-data
    GST_CAT_DEBUG(gst_realsense_src_debug, "setting timestamp.");        
    GST_BUFFER_TIMESTAMP(*buf) =
    GST_CLOCK_DIFF(gst_element_get_base_time(GST_ELEMENT(src)), clock_time);
    GST_BUFFER_DTS(*buf) = GST_BUFFER_TIMESTAMP(*buf);
    GST_BUFFER_OFFSET(*buf) = temp_ugly_buf_index++;
    // <---- Timestamp meta-data
    ++(src->frame_count);
    GST_LOG_OBJECT(src, "Creating meta data depth info for rgb"); 
    gst_buffer_unmap(*buf, &minfo);   
    return src->stop_requested ? GST_FLOW_FLUSHING : GST_FLOW_OK;

    } catch (const rs2::error& e) {
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
            ("RealSense error calling %s (%s)",
            e.get_failed_function().c_str(), e.get_failed_args().c_str()),
            (NULL));
        return GST_FLOW_ERROR;
    }
}

static gboolean
gst_realsense_src_start(GstBaseSrc* basesrc)
{
    auto* src = GST_REALSENSESRC(basesrc);
    GST_TRACE_OBJECT(src, "gst_realsense_src_start");

    // Validate color and depth mode before starting pipeline
    if (!is_valid_mode(valid_color_modes, src->color_width, src->color_height, src->color_fps)) {
        GST_ELEMENT_ERROR(src, RESOURCE, SETTINGS,
            ("Invalid color mode: %dx%d@%d. Not starting pipeline.", src->color_width, src->color_height, src->color_fps), (NULL));
        return FALSE;
    }
    if (!is_valid_mode(valid_depth_modes, src->depth_width, src->depth_height, src->depth_fps)) {
        GST_ELEMENT_ERROR(src, RESOURCE, SETTINGS,
            ("Invalid depth mode: %dx%d@%d. Not starting pipeline.", src->depth_width, src->depth_height, src->depth_fps), (NULL));
        return FALSE;
    }

    try {
        GST_LOG_OBJECT(src, "Creating RealSense pipeline");
        src->rs_pipeline = std::make_unique<rs2::pipeline>();
        if (!src->rs_pipeline) {
            GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("Failed to create RealSense pipeline."), (NULL));
            return FALSE;
        }

        rs2::config cfg;
        rs2::context ctx;
        const auto dev_list = ctx.query_devices();
        std::string serial_number;

        if (dev_list.size() == 0) {
            GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
                ("No RealSense devices found. Cannot start pipeline."),
                (NULL));
            return FALSE;
        }

        serial_number = std::string(dev_list[0].get_info(RS2_CAMERA_INFO_SERIAL_NUMBER));

        // -----> Load ShortRangePreset.json for D435i
        if (strcmp(dev_list[0].get_info(RS2_CAMERA_INFO_NAME), "Intel RealSense D435I") == 0) {
            if (src->preset_file && src->preset_file[0] != '\0') {
                std::string json_file = src->preset_file;
                GST_INFO_OBJECT(src, "Preset file path at start: %s", json_file.c_str());
                auto advanced_mode_dev = dev_list[0].as<rs400::advanced_mode>();

                if (!advanced_mode_dev.is_enabled()) {
                    advanced_mode_dev.toggle_advanced_mode(true);
                    GST_LOG_OBJECT(src, "Advanced mode enabled.");
                }

                std::ifstream f(json_file);
                if (!f) {
                    GST_ELEMENT_WARNING(src, RESOURCE, SETTINGS,
                        ("Could not open preset file: %s", json_file.c_str()), (NULL));
                } else {
                    std::string preset((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    advanced_mode_dev.load_json(preset);
                }
            } // else: no preset file set, use camera's default configuration
        } else {
            GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
                ("Selected device is not an Intel RealSense D435i."),
                (NULL));
            return FALSE;
        }

        cfg.enable_device(serial_number);
        cfg.enable_stream(RS2_STREAM_COLOR, src->color_width, src->color_height, RS2_FORMAT_RGB8, src->color_fps);
        cfg.enable_stream(RS2_STREAM_DEPTH, src->depth_width, src->depth_height, RS2_FORMAT_Z16, src->depth_fps);

        // -----> Handle stream alignment (Color or Depth)
        switch (src->align) {
            case Align::None:
                break;
            case Align::Color:
                src->aligner = std::make_unique<rs2::align>(RS2_STREAM_COLOR);
                break;
            case Align::Depth:
                src->aligner = std::make_unique<rs2::align>(RS2_STREAM_DEPTH);
                break;
            default:
                GST_ELEMENT_WARNING(src, RESOURCE, SETTINGS,
                    ("Unknown alignment parameter %d", src->align), (NULL));
        }

        // -----> Start the RealSense pipeline
        src->rs_pipeline->start(cfg);

        GST_LOG_OBJECT(src, "RealSense pipeline started");

        // Calculate caps using actual RealSense output
        if (!gst_realsense_src_calculate_caps(src)) {
            return FALSE;
        }

    } catch (const rs2::error& e) {
        GST_ERROR_OBJECT(src, "RealSense error calling %s (%s)",
            e.get_failed_function().c_str(),
            e.get_failed_args().c_str());
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED,
            ("RealSense error calling %s (%s)",
                e.get_failed_function().c_str(),
                e.get_failed_args().c_str()),
            (NULL));
        return FALSE;
    }

    return TRUE;
}

