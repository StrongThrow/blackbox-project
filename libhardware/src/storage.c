// storage.c
#include "hardware.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

static GstElement *pipeline = NULL;
static GstElement *appsrc   = NULL;

static bool     g_recording = false;
static bool     g_caps_set  = false;
static int      g_fps       = 30;
static guint64  g_frame_idx = 0;

static void reset_state(void) {
    pipeline = NULL;
    appsrc   = NULL;
    g_recording = false;
    g_caps_set  = false;
    g_frame_idx = 0;
}

int storage_start_recording(const char* filename)
{
    if (!filename || !filename[0]) return -1;
    if (g_recording) return -2;

    // 1) GStreamer 초기화(여러 번 호출해도 내부에서 한 번만 초기화됨)
    int argc = 0; char **argv = NULL;
    gst_init(&argc, &argv);

    // 2) 요소 생성
    appsrc = gst_element_factory_make("appsrc", "mysrc");
    GstElement *conv   = gst_element_factory_make("videoconvert", "conv");
    // HW 인코더 우선, 없으면 x264로 폴백
    GstElement *enc    = gst_element_factory_make("v4l2h264enc", "enc");
    if (!enc) enc      = gst_element_factory_make("x264enc", "enc");
    GstElement *parse  = gst_element_factory_make("h264parse", "parse");
    GstElement *mux    = gst_element_factory_make("mp4mux", "mux");
    GstElement *sink   = gst_element_factory_make("filesink", "sink");

    if (!appsrc || !conv || !enc || !parse || !mux || !sink) {
        fprintf(stderr, "[storage] element create failed\n");
        return -3;
    }

    // 3) 속성 설정
    g_object_set(sink, "location", filename, NULL);
    g_object_set(mux, "faststart", TRUE, NULL);        // MP4 시크 빠르게
    g_object_set(appsrc,
                 "is-live", TRUE,
                 "format", GST_FORMAT_TIME,
                 "block", TRUE,               // 백프레셔 시 블록
                 NULL);

    // x264일 경우 레이턴시/속도 설정
    if (g_strcmp0(G_OBJECT_TYPE_NAME(enc), "Gstx264Enc") == 0) {
        g_object_set(enc,
                     "tune", 0x00000004 /* zerolatency */,
                     "speed-preset", 1 /* ultrafast */,
                     NULL);
    }

    // 4) 파이프라인 조립
    pipeline = gst_pipeline_new("rec-pipeline");
    gst_bin_add_many(GST_BIN(pipeline), appsrc, conv, enc, parse, mux, sink, NULL);
    if (!gst_element_link_many(appsrc, conv, enc, parse, mux, sink, NULL)) {
        fprintf(stderr, "[storage] link failed\n");
        gst_object_unref(pipeline);
        reset_state();
        return -4;
    }

    // 5) 플레이
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        fprintf(stderr, "[storage] set_state PLAYING failed\n");
        gst_object_unref(pipeline);
        reset_state();
        return -5;
    }

    g_recording = true;
    g_caps_set  = false;
    g_frame_idx = 0;
    return 0;
}

int storage_write_frame(const FrameBuffer* frame)
{
    if (!g_recording || !pipeline || !appsrc || !frame || !frame->data) return -1;

    // 최초 프레임에서 caps 지정(RGB, width/height, fps)
    if (!g_caps_set) {
        GstCaps *caps = gst_caps_new_simple("video/x-raw",
            "format",   G_TYPE_STRING, "RGB",
            "width",    G_TYPE_INT,    frame->width,
            "height",   G_TYPE_INT,    frame->height,
            "framerate",GST_TYPE_FRACTION, g_fps, 1,
            NULL);
        g_object_set(appsrc, "caps", caps, NULL);
        gst_caps_unref(caps);
        g_caps_set = true;
    }

    // 프레임 데이터 -> GstBuffer
    GstBuffer *buffer = gst_buffer_new_allocate(NULL, frame->size, NULL);
    if (!buffer) return -2;

    gst_buffer_fill(buffer, 0, frame->data, frame->size);

    // 타임스탬프(PTS/DURATION)
    GstClockTime duration = gst_util_uint64_scale_int(GST_SECOND, 1, g_fps);
    GST_BUFFER_PTS(buffer)      = g_frame_idx * duration;
    GST_BUFFER_DTS(buffer)      = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buffer) = duration;
    g_frame_idx++;

    // push
    GstFlowReturn flow = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
    if (flow != GST_FLOW_OK) {
        fprintf(stderr, "[storage] push flow=%d\n", flow);
        return -3;
    }
    return 0;
}

void storage_stop_recording(void)
{
    if (!g_recording || !pipeline || !appsrc) { reset_state(); return; }

    // EOS
    gst_app_src_end_of_stream(GST_APP_SRC(appsrc));

    // 파이프라인 종료
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    reset_state();
}
