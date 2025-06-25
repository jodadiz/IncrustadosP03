#include <gst/gst.h>
#include <glib.h>

static gboolean bus_call (GstBus *bus, GstMessage *msg, gpointer data) {
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);
      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);
      g_main_loop_quit (loop);
      break;
    }

    default:
      break;
  }

  return TRUE;
}

int main (int argc, char *argv[]) {
  GMainLoop *loop;
  GstElement *pipeline, *source, *capsfilter, *encoder, *parse, *pay, *sink;
  GstCaps *caps;
  GstBus *bus;
  guint bus_watch_id;

  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  // Crear elementos
  pipeline   = gst_pipeline_new ("video-stream-pipeline");
  source     = gst_element_factory_make ("nvarguscamerasrc", "source");
  capsfilter = gst_element_factory_make ("capsfilter",         "capsfilter");
  encoder    = gst_element_factory_make ("nvv4l2h264enc",       "encoder");
  parse      = gst_element_factory_make ("h264parse",           "parse");
  pay        = gst_element_factory_make ("rtph264pay",          "payloader");
  sink       = gst_element_factory_make ("udpsink",             "sink");

  if (!pipeline || !source || !capsfilter || !encoder || !parse || !pay || !sink) {
    g_printerr ("No se pudo crear uno o más elementos.\n");
    return -1;
  }

  // Configurar elementos
  caps = gst_caps_from_string ("video/x-raw(memory:NVMM), format=NV12, width=1920, height=1080");
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  g_object_set (encoder, "insert-sps-pps", TRUE, NULL);
  g_object_set (pay, "pt", 96, NULL);
  g_object_set (sink, "host", "192.168.0.13", "port", 8001, "sync", FALSE, NULL);

  // Agregar elementos al pipeline
  gst_bin_add_many (GST_BIN (pipeline), source, capsfilter, encoder, parse, pay, sink, NULL);

  // Enlazar elementos
  if (!gst_element_link_many (source, capsfilter, encoder, parse, pay, sink, NULL)) {
    g_printerr ("Error al enlazar los elementos.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  // Escuchar mensajes
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  // Iniciar ejecución
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_print ("Ejecutando...\n");
  g_main_loop_run (loop);

  // Finalizar
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;
}