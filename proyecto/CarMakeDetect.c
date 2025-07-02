#include <gst/gst.h>
#include <glib.h>
#include <string.h>

// Variable global para poder acceder al pipeline
static GstElement *global_pipeline = NULL;

// Función para manejar Ctrl+C. Envía un evento EOS al pipeline.
void handle_sigint(int sig) {
  if (global_pipeline) {
    g_print("\nCtrl+C detectado, enviando EOS...\n");
    gst_element_send_event(global_pipeline, gst_event_new_eos());
  }
}

// Función mensajes del bus (fin de stream, error)
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  GMainLoop *loop = (GMainLoop *)data;

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

// Callback para enlazar dinámicamente pads
void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
  GstElement *other_element = (GstElement *)data;
  GstPad *sinkpad = gst_element_get_static_pad(other_element, "sink");
  if (!gst_pad_is_linked(sinkpad)) {
    gst_pad_link(pad, sinkpad); // Enlaza el pad dinámico al pad sink
  }
  gst_object_unref(sinkpad);
}

int main(int argc, char *argv[]) {

  // Revisar que se pase un argumento
  if (argc != 2) {
    g_printerr("Uso: %s <1=MP4 | 2=Cámara>\n", argv[0]);
    return -1;
  }

  int mode = atoi(argv[1]); // Se guarda el argumento como mode: 1 para MP4, 2 para IMX219
  GMainLoop *loop;
  GstElement *pipeline;
  GstBus *bus;
  guint bus_watch_id;

  gst_init(&argc, &argv); // Inicializa GStreamer
  loop = g_main_loop_new(NULL, FALSE);

  pipeline = gst_pipeline_new("pipeline"); // Crea pipeline principal

  // Declaración de elementos
  GstElement *queue2, *nvvideoconvert2, *queue3, *nvinfer1, *queue4, *nvtracker, *queue5;
  GstElement *nvinfer2, *queue6, *nvdsosd, *tee;
  GstElement *queue_disp, *nvoverlaysink;
  GstElement *queue_udp, *nvvideoconvert3, *encoder_udp, *parser_udp, *pay_udp, *sink_udp;
  GstElement *queue_file, *nvvideoconvert4, *encoder_file, *parser_file, *mux, *sink_file;

  // Crear elementos comunes que se utilizan en para MP4 como para IMX219
  queue2 = gst_element_factory_make("queue", "queue2");
  nvvideoconvert2 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert2");
  queue3 = gst_element_factory_make("queue", "queue3");
  nvinfer1 = gst_element_factory_make("nvinfer", "nvinfer1");
  queue4 = gst_element_factory_make("queue", "queue4");
  nvtracker = gst_element_factory_make("nvtracker", "nvtracker");
  queue5 = gst_element_factory_make("queue", "queue5");
  nvinfer2 = gst_element_factory_make("nvinfer", "nvinfer2");
  queue6 = gst_element_factory_make("queue", "queue6");
  nvdsosd = gst_element_factory_make("nvdsosd", "nvdsosd");
  tee = gst_element_factory_make("tee", "tee");

  // Utilizados en la rama del DISPLAY
  queue_disp = gst_element_factory_make("queue", "queue_disp");
  nvoverlaysink = gst_element_factory_make("nvoverlaysink", "nvoverlaysink");

  // Utilizados en la rama del UDP
  queue_udp = gst_element_factory_make("queue", "queue_udp");
  nvvideoconvert3 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert3");
  encoder_udp = gst_element_factory_make("nvv4l2h264enc", "encoder_udp");
  parser_udp = gst_element_factory_make("h264parse", "parser_udp");
  pay_udp = gst_element_factory_make("rtph264pay", "pay_udp");
  sink_udp = gst_element_factory_make("udpsink", "sink_udp");

  // Utilizados en la rama del ARCHIVO
  queue_file = gst_element_factory_make("queue", "queue_file");
  nvvideoconvert4 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert4");
  encoder_file = gst_element_factory_make("nvv4l2h264enc", "encoder_file");
  parser_file = gst_element_factory_make("h264parse", "parser_file");
  mux = gst_element_factory_make("qtmux", "mux");
  sink_file = gst_element_factory_make("filesink", "sink_file");

  // Configuración de las interferecias necesarias para aplicar los modelos
  g_object_set(nvinfer1,
               "config-file-path", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary_nano.txt",
               "model-engine-file", "/opt/nvidia/deepstream/deepstream-6.0/samples/models/Primary_Detector_Nano/resnet10.caffemodel_b8_gpu0_fp16.engine",
               "unique-id", 1, NULL);

  g_object_set(nvtracker,
               "tracker-width", 640,
               "tracker-height", 368,
               "ll-lib-file", "/opt/nvidia/deepstream/deepstream-6.0/lib/libnvds_nvmultiobjecttracker.so",
               "ll-config-file", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_tracker_IOU.yml",
               "enable-batch-process", 1, NULL);

  g_object_set(nvinfer2,
               "process-mode", 2,
               "infer-on-gie-id", 1,
               "infer-on-class-ids", "0:",
               "batch-size", 1,
               "config-file-path", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_secondary_carmake.txt",
               "model-engine-file", "/opt/nvidia/deepstream/deepstream-6.0/samples/models/Secondary_CarMake/resnet18.caffemodel_b1_gpu0_fp16.engine",
               "unique-id", 2, NULL);

  g_object_set(nvdsosd, "process-mode", 0, NULL);
  g_object_set(sink_udp, "host", "192.168.100.117", "port", 8001, "sync", FALSE, NULL);
  g_object_set(sink_file, "location", "naruto.mp4", "async", FALSE, NULL);
  g_object_set(encoder_udp, "insert-sps-pps", TRUE, NULL);
  g_object_set(encoder_file, "insert-sps-pps", TRUE, NULL);
  g_object_set(pay_udp, "pt", 96, "config-interval", 1, NULL);

  // Se selecciona la FUENTE segun el MODE elegido
  GstElement *src = NULL, *capsfilter = NULL, *nvvidconv1 = NULL, *nvvideoconvert1 = NULL, *queue1 = NULL, *streammux = NULL;
  if (mode == 1) {

    // Fuente archivo MP4
    GstElement *source = gst_element_factory_make("filesrc", "source");
    GstElement *qtdemux = gst_element_factory_make("qtdemux", "qtdemux");
    GstElement *h264parse1 = gst_element_factory_make("h264parse", "h264parse1");
    GstElement *decoder = gst_element_factory_make("nvv4l2decoder", "decoder");
    queue1 = gst_element_factory_make("queue", "queue1");
    streammux = gst_element_factory_make("nvstreammux", "streammux");

    g_object_set(source, "location", "/opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4", NULL);
    g_object_set(streammux, "batch-size", 1, "width", 1920, "height", 1080, "num-surfaces-per-frame", 1, NULL);

    // Añadir elementos al pipeline para MP4
    gst_bin_add_many(GST_BIN(pipeline), source, qtdemux, h264parse1, decoder, queue1, streammux, queue2,
                     nvvideoconvert2, queue3, nvinfer1, queue4, nvtracker, queue5, nvinfer2, queue6, nvdsosd, tee,
                     queue_disp, nvoverlaysink, queue_udp, nvvideoconvert3, encoder_udp, parser_udp, pay_udp, sink_udp,
                     queue_file, nvvideoconvert4, encoder_file, parser_file, mux, sink_file, NULL);

    // Enlazar los archivo
    gst_element_link(source, qtdemux);
    g_signal_connect(qtdemux, "pad-added", G_CALLBACK(on_pad_added), h264parse1);
    gst_element_link_many(h264parse1, decoder, queue1, NULL);

    // Conexión dinámica a streammux
    GstPad *srcpad = gst_element_get_static_pad(queue1, "src");
    GstPad *sinkpad = gst_element_get_request_pad(streammux, "sink_0");
    gst_pad_link(srcpad, sinkpad);
    gst_object_unref(srcpad);
    gst_object_unref(sinkpad);

  } else if (mode == 2) {

    // Fuente cámara IMX219
    src = gst_element_factory_make("nvarguscamerasrc", "camera-source");
    capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    nvvidconv1 = gst_element_factory_make("nvvidconv", "nvvidconv1");
    nvvideoconvert1 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert1");
    queue1 = gst_element_factory_make("queue", "queue1");
    streammux = gst_element_factory_make("nvstreammux", "streammux");

    g_object_set(src, "bufapi-version", TRUE, NULL);

    GstCaps *caps = gst_caps_from_string("video/x-raw(memory:NVMM), width=1280, height=720, format=NV12, framerate=60/1");
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    g_object_set(streammux, "batch-size", 1, "width", 1280, "height", 720, "live-source", 1, NULL);

    // Añadir elementos al pipeline
    gst_bin_add_many(GST_BIN(pipeline), src, capsfilter, nvvidconv1, nvvideoconvert1, queue1, streammux, queue2,
                     nvvideoconvert2, queue3, nvinfer1, queue4, nvtracker, queue5, nvinfer2, queue6, nvdsosd, tee,
                     queue_disp, nvoverlaysink, queue_udp, nvvideoconvert3, encoder_udp, parser_udp, pay_udp, sink_udp,
                     queue_file, nvvideoconvert4, encoder_file, parser_file, mux, sink_file, NULL);

    // Enlazar cámara
    gst_element_link_many(src, capsfilter, nvvidconv1, nvvideoconvert1, queue1, NULL);
    gst_element_link_pads(queue1, "src", streammux, "sink_0");
  } else {
    g_printerr("Modo inválido: 1=MP4, 2=Cámara\n");
    return -1;
  }

  // Enlace del procesamiento usando para ambos
  gst_element_link_many(streammux, queue2, nvvideoconvert2, queue3, nvinfer1, queue4, nvtracker, queue5, nvinfer2, queue6, nvdsosd, tee, NULL);

  // TEE del DISPLAY
  gst_element_link_many(queue_disp, nvoverlaysink, NULL);
  GstPad *tee_disp_pad = gst_element_get_request_pad(tee, "src_%u");
  GstPad *queue_disp_sink = gst_element_get_static_pad(queue_disp, "sink");
  gst_pad_link(tee_disp_pad, queue_disp_sink);
  gst_object_unref(tee_disp_pad);
  gst_object_unref(queue_disp_sink);

  // TEE del UDP
  gst_element_link_many(queue_udp, nvvideoconvert3, encoder_udp, parser_udp, pay_udp, sink_udp, NULL);
  GstPad *tee_udp_pad = gst_element_get_request_pad(tee, "src_%u");
  GstPad *queue_udp_sink = gst_element_get_static_pad(queue_udp, "sink");
  gst_pad_link(tee_udp_pad, queue_udp_sink);
  gst_object_unref(tee_udp_pad);
  gst_object_unref(queue_udp_sink);

  // TEE del ARCHIVO
  gst_element_link_many(queue_file, nvvideoconvert4, encoder_file, parser_file, mux, sink_file, NULL);
  GstPad *tee_file_pad = gst_element_get_request_pad(tee, "src_%u");
  GstPad *queue_file_sink = gst_element_get_static_pad(queue_file, "sink");
  gst_pad_link(tee_file_pad, queue_file_sink);
  gst_object_unref(tee_file_pad);
  gst_object_unref(queue_file_sink);

  // Configurar bus
  bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
  gst_object_unref(bus);

  // Registrar Ctrl+C para enviar EOS
  global_pipeline = pipeline;
  signal(SIGINT, handle_sigint);

  // Ejecutar
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_print("Pipeline ejecutándose...\n");
  g_main_loop_run(loop);

  // Finalizar
  g_print("Finalizando...\n");
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(GST_OBJECT(pipeline));
  g_source_remove(bus_watch_id);
  g_main_loop_unref(loop);

  return 0;
}

