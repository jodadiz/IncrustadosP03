#include <stdio.h>    
#include <gst/gst.h>
#include <glib.h>

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *) data;

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("End of stream\n");
            g_main_loop_quit(loop);
            break;

        case GST_MESSAGE_ERROR: {
            gchar *debug;
            GError *error;

            gst_message_parse_error(msg, &error, &debug);
            g_free(debug);
            g_printerr("Error: %s\n", error->message);
            g_error_free(error);
            g_main_loop_quit(loop);
            break;
        }

        default:
            break;
    }

    return TRUE;
}

int main(int argc, char *argv[]) {
    GMainLoop *loop;
    GstElement *pipeline, *source, *capsfilter, *queue1, *streammux, *queue2;
    GstElement *convert, *infer, *queue3, *osd, *convert2, *encoder, *parse, *pay, *sink;
    GstCaps *caps;
    GstBus *bus;
    guint bus_watch_id;

    gst_init(&argc, &argv);
    loop = g_main_loop_new(NULL, FALSE);

    pipeline   = gst_pipeline_new("deepstream-udp-pipeline");
    source     = gst_element_factory_make("nvarguscamerasrc", "source");
    capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    queue1     = gst_element_factory_make("queue", "queue1");
    streammux  = gst_element_factory_make("nvstreammux", "streammux");
    queue2     = gst_element_factory_make("queue", "queue2");
    convert    = gst_element_factory_make("nvvideoconvert", "convert");
    infer      = gst_element_factory_make("nvinfer", "infer");
    queue3     = gst_element_factory_make("queue", "queue3");
    osd        = gst_element_factory_make("nvdsosd", "osd");
    convert2   = gst_element_factory_make("nvvideoconvert", "convert2");
    encoder    = gst_element_factory_make("nvv4l2h264enc", "encoder");
    parse      = gst_element_factory_make("h264parse", "parser");
    pay        = gst_element_factory_make("rtph264pay", "pay");
    sink       = gst_element_factory_make("udpsink", "sink");

    if (!pipeline || !source || !capsfilter || !queue1 || !streammux || !queue2 || !convert ||
        !infer || !queue3 || !osd || !convert2 || !encoder || !parse || !pay || !sink) {
        g_printerr("Error: No se pudieron crear todos los elementos.\n");
        return -1;
    }

    caps = gst_caps_from_string("video/x-raw(memory:NVMM), format=NV12, width=1920, height=1080");
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    g_object_set(source, "bufapi-version", TRUE, "nvbuf-memory-type", 0, "sensor-id", 0, NULL);

    g_object_set(streammux,
                 "width", 1920,
                 "height", 1080,
                 "batch-size", 1,
                 "live-source", TRUE,
                 "num-surfaces-per-frame", 1,
                 NULL);

    g_object_set(infer,
                 "config-file-path", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary.txt",
                 "model-engine-file", "/opt/nvidia/deepstream/deepstream-6.0/samples/models/Primary_Detector/resnet10.caffemodel_b1_gpu0_fp16.engine",
                 NULL);

    g_object_set(osd, "process-mode", 0, NULL);
    g_object_set(encoder, "insert-sps-pps", TRUE, NULL);
    g_object_set(pay, "pt", 96, NULL);
    g_object_set(sink,
                 "host", "192.168.0.13",
                 "port", 8001,
                 "sync", FALSE,
                 NULL);

    gst_bin_add_many(GST_BIN(pipeline),
                     source, capsfilter, queue1,
                     streammux, queue2, convert, infer,
                     queue3, osd, convert2, encoder, parse, pay, sink,
                     NULL);

    GstPad *sinkpad, *srcpad;
    gchar pad_name[16];

    gst_element_link_many(source, capsfilter, queue1, NULL);
    srcpad = gst_element_get_static_pad(queue1, "src");
    snprintf(pad_name, sizeof(pad_name), "sink_0");
    sinkpad = gst_element_get_request_pad(streammux, pad_name);

    if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK) {
        g_printerr("No se pudo enlazar streammux.sink_0\n");
        return -1;
    }
    gst_object_unref(srcpad);

    if (!gst_element_link_many(streammux, queue2, convert, infer, queue3, osd, convert2, encoder, parse, pay, sink, NULL)) {
        g_printerr("Error al enlazar el resto del pipeline.\n");
        return -1;
    }

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("Pipeline ejecut\u00e1ndose...\n");
    g_main_loop_run(loop);

    g_print("Finalizando...\n");
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(pipeline));
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);

    return 0;
}
