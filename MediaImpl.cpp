/*
 * MediaImpl.cpp
 *
 * (c) 2013 Sofian Audry -- info(@)sofianaudry(.)com
 * (c) 2013 Alexandre Quessy -- alexandre(@)quessy(.)net
 * (c) 2012 Jean-Sebastien Senecal
 * (c) 2004 Mathieu Guindon, Julien Keable
 *           Based on code from Drone http://github.com/sofian/drone
 *           Based on code from the GStreamer Tutorials http://docs.gstreamer.com/display/GstSDK/Tutorials
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "MediaImpl.h"
#include <cstring>
#include <iostream>

// -------- private implementation of VideoImpl -------

bool MediaImpl::hasVideoSupport()
{
  static bool did_print_gst_version = false;
  if (! did_print_gst_version)
  {
    qDebug() << "Using GStreamer version " <<
      GST_VERSION_MAJOR << "." <<
      GST_VERSION_MINOR << "." <<
      GST_VERSION_MICRO;
    did_print_gst_version = true;
  }
  // TODO: actually check if we have it
  return true;
}

int MediaImpl::getWidth() const
{
  Q_ASSERT(videoIsConnected());
  return _padHandlerData.width;
}

int MediaImpl::getHeight() const
{
  Q_ASSERT(videoIsConnected());
  return _padHandlerData.height;
}

const uchar* MediaImpl::getBits()
{
  // Reset bits changed.
  _bitsChanged = false;

  // Return data.
  if (_currentFrameSample == NULL)
    return NULL;
  else
    return _data;
}

QString MediaImpl::getUri() const
{
  return _uri;
}

bool MediaImpl::getAttached()
{
  return _attached;
}

void MediaImpl::setAttached(bool attach)
{
  _attached = attach;
}


void MediaImpl::setRate(double rate)
{
  if (rate == 0)
  {
    qDebug() << "Cannot set rate to zero, ignoring rate " << rate << endl;
    return;
  }

  // Only update rate if needed.
  if (_rate != rate)
  {
    _rate = rate;

    // Send seek events to activate rate.
    if (_seekEnabled)
      _updateRate();
  }
}

void MediaImpl::build()
{
  qDebug() << "Building video impl";
  if (!loadMovie(_uri))
  {
    qDebug() << "Cannot load movie " << _uri << ".";
  }
}

MediaImpl::~MediaImpl()
{
  // Free all resources.
  freeResources();

  // Free mutex locker object.
  delete _mutexLocker;
}

bool MediaImpl::_eos() const
{
  if (_movieReady)
  {
    Q_ASSERT( _appsink0 );
    if (_rate > 0)
    {
      gboolean videoEos;
      g_object_get (G_OBJECT (_appsink0), "eos", &videoEos, NULL);
      return (bool) (videoEos);
    }
    else
    {
      /* Obtain the current position, needed for the seek event */
      gint64 position;
      if (!gst_element_query_position (_pipeline, GST_FORMAT_TIME, &position)) {
        g_printerr ("Unable to retrieve current position.\n");
        return false;
      }
      return (position == 0);
    }
  }
  else
    return false;
}

GstFlowReturn MediaImpl::gstNewSampleCallback(GstElement*, MediaImpl *p)
{
  // Make it thread-safe.
  p->lockMutex();

  // Get next frame.
  GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(p->_appsink0));

  // Unref last frame.
  p->_freeCurrentSample();

  // Set current frame.
  p->_currentFrameSample = sample;

  // For live sources, video dimensions have not been set, because
  // gstPadAddedCallback is never called. Fix dimensions from first sample /
  // caps we receive.
  if (p->_isSharedMemorySource && ( p->_padHandlerData.width == -1 ||
        p->_padHandlerData.height == -1)) {
    GstCaps *caps = gst_sample_get_caps(sample);
    GstStructure *structure;
    structure = gst_caps_get_structure(caps, 0);
    gst_structure_get_int(structure, "width",  &p->_padHandlerData.width);
    gst_structure_get_int(structure, "height", &p->_padHandlerData.height);
    // g_print("Size is %u x %u\n", _padHandlerData.width, _padHandlerData.height);
  }

  // Try to retrieve data bits of frame.
  GstMapInfo& map = p->_mapInfo;
  GstBuffer *buffer = gst_sample_get_buffer( sample );
  if (gst_buffer_map(buffer, &map, GST_MAP_READ))
  {
    p->_currentFrameBuffer = buffer;
    // For debugging:
    //gst_util_dump_mem(map.data, map.size)

    // Retrieve data from map info.
    p->_data = map.data;

    // Bits have changed.
    p->_bitsChanged = true;
  }

  p->unlockMutex();

  return GST_FLOW_OK;
}

MediaImpl::MediaImpl(const QString uri, bool live) :
_bus(NULL),
_pipeline(NULL),
_uridecodebin0(NULL),
_queue0(NULL),
_videoconvert0(NULL),
//_audioSink(NULL),
_appsink0(NULL),
_currentFrameSample(NULL),
_currentFrameBuffer(NULL),
_bitsChanged(false),
_width(640), // unused
_height(480), // unused
_data(NULL),
_seekEnabled(false),
_rate(1.0),
_isSharedMemorySource(live),
_attached(false),
_movieReady(false),
_playState(false),
_uri(uri)
{
  _pollSource = NULL;
  if (uri != "")
  {
    loadMovie(uri);
  }
  _mutexLocker = new QMutexLocker(&_mutex);
}

void MediaImpl::unloadMovie()
{
  // Free allocated resources.
  freeResources();

  // Reset variables.
  _terminate = false;
  _seekEnabled = false;

  // Un-ready.
  _setMovieReady(false);
  setPlayState(false);
}

void MediaImpl::freeResources()
{
  // Free resources.
  if (_bus)
  {
    gst_object_unref (_bus);
    _bus = NULL;
  }

  if (_pipeline)
  {
    gst_element_set_state (_pipeline, GST_STATE_NULL);
    gst_object_unref (_pipeline);
    _pipeline = NULL;
  }

  // Reset pipeline elements.
  _uridecodebin0 = NULL;
  _queue0 = NULL;
  _videoconvert0 = NULL;
  _appsink0 = NULL;

  // Reset pad handler.
  _padHandlerData = GstPadHandlerData();

  // Unref the shmsrc poller.
  if (_pollSource)
  {
     g_source_unref(_pollSource);
     _pollSource = NULL;
  }

  qDebug() << "Freeing remaining samples/buffers" << endl;

  // Frees current sample and buffer.
  _freeCurrentSample();

  // Resets bits changed.
  _bitsChanged = false;
}

void MediaImpl::resetMovie()
{
  // XXX: There used to be an issue that when we reached EOS (_eos() == true) we could not seek anymore.
  if (_seekEnabled)
  {
    qDebug() << "Seeking at position 0.";
    GstEvent* seek_event;

    if (_rate > 0) {
      seek_event = gst_event_new_seek (_rate, GST_FORMAT_TIME, GstSeekFlags( GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE ),
          GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, 0);
    } else {
      seek_event = gst_event_new_seek (_rate, GST_FORMAT_TIME, GstSeekFlags( GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE ),
          GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_END, 0);
    }
    /* Send the event */
    gst_element_send_event (_appsink0, seek_event);
//    gst_element_seek_simple (_pipeline, GST_FORMAT_TIME,
//                             (GstSeekFlags) (GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), 0);
//    this->_currentFrameSample = NULL;
    _setMovieReady(true);
  }
  else
  {
    // Just reload movie.
    qDebug() << "Reloading the movie" << _seekEnabled;
    loadMovie(_uri);
  }
}

gboolean 
gstPollShmsrc (void *user_data)
{
  MediaImpl *p = (MediaImpl*) user_data;
  if (g_file_test(p->getUri().toUtf8().constData(), G_FILE_TEST_EXISTS) &&
    ! p->getAttached())
  {
    if (! p->setPlayState(true))
    {
      qDebug() << "tried to attach, but starting pipeline failed!" << endl;
      return false;
    }
    //qDebug() << "attached, started pipeline!" << endl;
    p->setAttached(true);
  }
  return true;
}

bool MediaImpl::loadMovie(QString filename)
{
  gchar* filetestpath = (gchar*) filename.toUtf8().constData();
  if (FALSE == g_file_test(filetestpath, G_FILE_TEST_EXISTS))
  {
      std::cout << "File " << filetestpath << " does not exist" << std::endl;
      return false;
  }
  _uri = filename;

  qDebug() << "Opening movie: " << filename << ".";

  // Free previously allocated structures
  unloadMovie();

  // Initialize GStreamer.
  GstElement *capsfilter0 = NULL;
  GstElement *videoscale0 = NULL;

  // Create the elements.
  if (_isSharedMemorySource)
  {
    _shmsrc0 = gst_element_factory_make ("shmsrc", "shmsrc0");
    _gdpdepay0 = gst_element_factory_make ("gdpdepay", "gdpdepay0");
    _pollSource = g_timeout_source_new (500);
    g_source_set_callback (_pollSource, 
        gstPollShmsrc, 
        this, 
        NULL);
    g_source_attach (_pollSource, g_main_context_default());
    g_source_unref (_pollSource);
  }
  else {
    _uridecodebin0 = gst_element_factory_make ("uridecodebin", "uridecodebin0");
  }
  _queue0 = gst_element_factory_make ("queue", "queue0");
  _videoconvert0 = gst_element_factory_make ("videoconvert", "videoconvert0");
  videoscale0 = gst_element_factory_make ("videoscale", "videoscale0");
  capsfilter0 = gst_element_factory_make ("capsfilter", "capsfilter0");
  _appsink0 = gst_element_factory_make ("appsink", "appsink0");

  // Prepare handler data.
  _padHandlerData.videoToConnect = _queue0;
  _padHandlerData.videoSink = _appsink0;
  _padHandlerData.videoIsConnected = false;

  // Create the empty pipeline.
  _pipeline = gst_pipeline_new ( "video-source-pipeline" );

  if (!_pipeline ||
      !_queue0 || !_videoconvert0 || ! videoscale0 || ! capsfilter0 || ! _appsink0)
  {
    g_printerr ("Not all elements could be created.\n");

    if (! _pipeline) g_printerr("_pipeline");
    if (! _queue0) g_printerr("_queue0");
    if (! _videoconvert0) g_printerr("_videoconvert0");
    if (! videoscale0) g_printerr("videoscale0");
    if (! capsfilter0) g_printerr("capsfilter0");
    if (! _appsink0) g_printerr("_appsink0");

    unloadMovie();
    return -1;
  }
  
  if (_isSharedMemorySource)
  {
    if (! _shmsrc0 || ! _gdpdepay0)
    {
      g_printerr ("Not all elements could be created.\n");
      if (! _shmsrc0) g_printerr("_shmsrc0");
      if (! _gdpdepay0) g_printerr("_gdpdepay0");
      unloadMovie();
      return -1;
    }
  }
  else
  {
    if (! _uridecodebin0)
    {
      g_printerr ("Not all elements could be created.\n");
      if (! _uridecodebin0) g_printerr("_uridecodebin0");
      unloadMovie();
      return -1;
    }
  }

  // Build the pipeline. Note that we are NOT linking the source at this
  // point. We will do it later.
  gst_bin_add_many (GST_BIN (_pipeline),
    _isSharedMemorySource ? _shmsrc0 : _uridecodebin0, _queue0,
    _videoconvert0, videoscale0, capsfilter0, _appsink0, NULL);

  // special case for shmsrc
  if (_isSharedMemorySource)
  {
    gst_bin_add (GST_BIN(_pipeline), _gdpdepay0);
    if (! gst_element_link_many (_shmsrc0, _gdpdepay0, _queue0, NULL))
    {
      g_printerr ("Could not link shmsrc, deserializer and video queue.\n");
    }
  }
  else
  {
    if (! gst_element_link (_uridecodebin0, _queue0)) 
    {
      g_printerr ("Could not link uridecodebin to video queue.\n");
    }
  }

  if (! gst_element_link_many (_queue0, _videoconvert0, capsfilter0, videoscale0, _appsink0, NULL))
  {
    g_printerr ("Could not link video queue, colorspace converter, caps filter, scaler and app sink.\n");
    unloadMovie();
    return false;
  }

  // Process URI.
  QByteArray ba = filename.toLocal8Bit();
  gchar* uri = (gchar*) filename.toUtf8().constData();
  if (! _isSharedMemorySource && ! gst_uri_is_valid(uri))
  {
    // Try to convert filename to URI.
    GError* error = NULL;
    uri = gst_filename_to_uri(uri, &error);
    if (error)
    {
      qDebug() << "Filename to URI error: " << error->message;
      g_error_free(error);
      gst_object_unref (uri);
      freeResources();
      return false;
    }
  }

  if (_isSharedMemorySource)
  {
    uri =  (gchar*) ba.data();
  }

  // Set URI to be played.
  qDebug() << "URI for uridecodebin: " << uri;
  // FIXME: sometimes it's just the path to the directory that is given, not the file itself.

  // Connect to the pad-added signal
  if (! _isSharedMemorySource)
  {
    g_object_set (_uridecodebin0, "uri", uri, NULL);
    g_signal_connect (_uridecodebin0, "pad-added", G_CALLBACK (MediaImpl::gstPadAddedCallback), &_padHandlerData);
  }
  else
  {
    //qDebug() << "LIVE mode" << uri;
    g_object_set (_shmsrc0, "socket-path", uri, NULL);
    g_object_set (_shmsrc0, "is-live", TRUE, NULL);
    _padHandlerData.videoIsConnected = true;
  }

  // Configure audio appsink.
  // TODO: change from mono to stereo
//  gchar* audioCapsText = g_strdup_printf ("audio/x-raw-float,channels=1,rate=%d,signed=(boolean)true,width=%d,depth=%d,endianness=BYTE_ORDER",
//                                          Engine::signalInfo().sampleRate(), (int)(sizeof(Signal_T)*8), (int)(sizeof(Signal_T)*8) );
//  GstCaps* audioCaps = gst_caps_from_string (audioCapsText);
//  g_object_set (_audioSink, "emit-signals", TRUE,
//                            "caps", audioCaps,
////                            "max-buffers", 1,     // only one buffer (the last) is maintained in the queue
////                            "drop", TRUE,         // ... other buffers are dropped
//                            NULL);
//  g_signal_connect (_audioSink, "new-buffer", G_CALLBACK (VideoImpl::gstNewAudioBufferCallback), &_newAudioBufferHandlerData);
//  gst_caps_unref (audioCaps);
//  g_free (audioCapsText);

  // Configure video appsink.
  GstCaps *videoCaps = gst_caps_from_string ("video/x-raw,format=RGBA");
  g_object_set (capsfilter0, "caps", videoCaps, NULL);
  g_object_set (_appsink0, "emit-signals", TRUE,
                            "max-buffers", 1,     // only one buffer (the last) is maintained in the queue
                            "drop", TRUE,         // ... other buffers are dropped
                            "sync", TRUE,
                            NULL);
  g_signal_connect (_appsink0, "new-sample", G_CALLBACK (MediaImpl::gstNewSampleCallback), this);
  gst_caps_unref (videoCaps);

  // Listen to the bus.
  _bus = gst_element_get_bus (_pipeline);

  // Start playing.
  if (! _isSharedMemorySource && ! setPlayState(true))
  {
    return false;
  }

  return true;
}

void MediaImpl::update()
{
  // Check for end-of-stream or terminate.
  if (_eos() || _terminate)
  {
    _setFinished(true);
    resetMovie();
  }
  else
  {
    _setFinished(false);
  }

//  // Check if movie is ready and connected.
//  if (!isReady())
//  {
//    _bitsChanged = false;
//  }
//
  // Check gstreamer messages on bus.
  _checkMessages();
}

bool MediaImpl::setPlayState(bool play)
{
  if (_pipeline == NULL)
  {
    return false;
  }

  GstStateChangeReturn ret = gst_element_set_state (_pipeline, (play ? GST_STATE_PLAYING : GST_STATE_PAUSED));
  if (ret == GST_STATE_CHANGE_FAILURE)
  {
    qDebug() << "Unable to set the pipeline to the playing state.";
    unloadMovie();
    return false;
  }
  else
  {
    _playState = play;
    return true;
  }
}

//bool MediaImpl::_preRun()
//{
//  // Check for end-of-stream or terminate.
//  if (_eos() || _terminate)
//  {
//    _setFinished(true);
//    resetMovie();
//  }
//  else
//  {
//    _setFinished(false);
//  }
//  if (!_movieReady ||
//      !_padHandlerData.videoIsConnected)
//  {
//    return false;
//  }
//  return true;
//}

void MediaImpl::_checkMessages()
{
  if (_bus != NULL)
  {
    // Get message.
    GstMessage *msg = gst_bus_timed_pop_filtered(
                        _bus, 0,
                        (GstMessageType) (GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_ASYNC_DONE));

    if (msg != NULL)
    {
      GError *err;
      gchar *debug_info;

      switch (GST_MESSAGE_TYPE (msg))
      {
      // Error ////////////////////////////////////////////////
      case GST_MESSAGE_ERROR:
        gst_message_parse_error(msg, &err, &debug_info);
        g_printerr("Error received from element %s: %s\n",
            GST_OBJECT_NAME (msg->src), err->message);
        g_printerr("Debugging information: %s\n",
            debug_info ? debug_info : "none");
        g_clear_error(&err);
        g_free(debug_info);

        if (!_isSharedMemorySource)
        {
          _terminate = true;
        }
        else
        {
          _attached = false;
          gst_element_set_state (_pipeline, GST_STATE_PAUSED);
          gst_element_set_state (_pipeline, GST_STATE_NULL);
          gst_element_set_state (_pipeline, GST_STATE_READY);
        }
//        _finish();
        break;

      // End-of-stream ////////////////////////////////////////
      case GST_MESSAGE_EOS:
        // Automatically loop back.
        g_print("End-Of-Stream reached.\n");
        resetMovie();
//        _terminate = true;
//        _finish();
        break;

      // Pipeline has prerolled/ready to play ///////////////
      case GST_MESSAGE_ASYNC_DONE:
        if (!_isMovieReady())
        {
          // Check if seeking is allowed.
          gint64 start, end;
          GstQuery *query = gst_query_new_seeking (GST_FORMAT_TIME);
          if (gst_element_query (_pipeline, query))
          {
            gst_query_parse_seeking (query, NULL, (gboolean*)&_seekEnabled, &start, &end);
            if (_seekEnabled)
            {
              g_print ("Seeking is ENABLED from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT "\n",
                       GST_TIME_ARGS (start), GST_TIME_ARGS (end));

            }
            else
            {
              g_print ("Seeking is DISABLED for this stream.\n");
            }
          }
          else
          {
            g_printerr ("Seeking query failed.");
          }

          gst_query_unref (query);

          // Movie is ready!
          _setMovieReady(true);
        }

        break;

      case GST_MESSAGE_STATE_CHANGED:
        // We are only interested in state-changed messages from the pipeline.
        if (GST_MESSAGE_SRC (msg) == GST_OBJECT (_pipeline))
        {
          GstState oldState, newState, pendingState;
          gst_message_parse_state_changed(msg, &oldState, &newState,
              &pendingState);
          g_print("Pipeline state for movie %s changed from %s to %s:\n",
              _uri.toUtf8().constData(),
              gst_element_state_get_name(oldState),
              gst_element_state_get_name(newState));
        }
        break;

      default:
        // We should not reach here.
        g_printerr("Unexpected message received.\n");
        break;
      }
      gst_message_unref(msg);
    }
  }
}

void MediaImpl::_setMovieReady(bool ready)
{
  _movieReady = ready;
}

void MediaImpl::_setFinished(bool finished)
{
  Q_UNUSED(finished);
  //  qDebug() << "Clip " << (finished ? "finished" : "not finished");
}

void  MediaImpl::_updateRate()
{
  if (_pipeline == NULL)
  {
    qDebug() << "Cannot set rate: no pipeline!" << endl;
    return;
  }

  if (!_seekEnabled)
  {
    qDebug() << "Cannot set rate: seek not working" << endl;
    return;
  }

  if (!_isMovieReady())
  {
    qDebug() << "Movie is not yet ready to play, cannot seek yet." << endl;
  }

  gint64 position;
  GstEvent *seekEvent;

  /* Obtain the current position, needed for the seek event */
  if (!gst_element_query_position (_pipeline, GST_FORMAT_TIME, &position)) {
    g_printerr ("Unable to retrieve current position.\n");
    return;
  }

  /* Create the seek event */
  if (_rate > 0) {
    seekEvent = gst_event_new_seek (_rate, GST_FORMAT_TIME, GstSeekFlags( GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE ),
        GST_SEEK_TYPE_SET, position, GST_SEEK_TYPE_NONE, 0);
  } else {
    seekEvent = gst_event_new_seek (_rate, GST_FORMAT_TIME, GstSeekFlags( GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE ),
        GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, position);
  }

  if (_appsink0 == NULL) {
    /* If we have not done so, obtain the sink through which we will send the seek events */
    g_object_get (_pipeline, "video-sink", &_appsink0, NULL);
  }

  /* Send the event */
  gst_element_send_event (_appsink0, seekEvent);

  g_print ("Current rate: %g\n", _rate);
}

void MediaImpl::_freeCurrentSample() {
  if (_currentFrameBuffer != NULL)
  {
    gst_buffer_unmap(_currentFrameBuffer, &_mapInfo);
  }

  if (_currentFrameSample != NULL)
  {
    gst_sample_unref(_currentFrameSample);
  }

  _currentFrameSample = NULL;
  _currentFrameBuffer = NULL;
  _data = NULL;
}

/**
 * FIXME: remove GOTO
 */
void MediaImpl::gstPadAddedCallback(GstElement *src, GstPad *newPad, MediaImpl::GstPadHandlerData* data)
{
  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (newPad), GST_ELEMENT_NAME (src));
  GstPad *sinkPad = NULL;

  // Check the new pad's type.
  GstCaps *newPadCaps = gst_pad_query_caps (newPad, NULL);
  GstStructure *newPadStruct = gst_caps_get_structure (newPadCaps, 0);
  const gchar *newPadType   = gst_structure_get_name (newPadStruct);
  gchar *newPadStructStr = gst_structure_to_string(newPadStruct);
  g_print("Structure is %s\n", newPadStructStr);
  g_free(newPadStructStr);
  if (g_str_has_prefix (newPadType, "video/x-raw"))
  {
    sinkPad = gst_element_get_static_pad (data->videoToConnect, "sink");
    gst_structure_get_int(newPadStruct, "width",  &data->width);
    gst_structure_get_int(newPadStruct, "height", &data->height);
  }
  else
  {
    g_print ("  It has type '%s' which is not raw audio/video. Ignoring.\n", newPadType);
    goto exit;
  }

  // If our converter is already linked, we have nothing to do here.
  if (gst_pad_is_linked (sinkPad))
  {
    // Best prefixes.
    if (g_str_has_prefix (newPadType, "audio/x-raw-float") ||
        g_str_has_prefix (newPadType, "video/x-raw-int") )
    {
      g_print ("  Found a better pad.\n");
      GstPad* oldPad = gst_pad_get_peer(sinkPad);
      gst_pad_unlink(oldPad, sinkPad);
      g_object_unref(oldPad);
    }
    else
    {
      g_print ("  We are already linked. Ignoring.\n");
      goto exit;
    }
  }

  // Attempt the link
  if (GST_PAD_LINK_FAILED (gst_pad_link (newPad, sinkPad)))
  {
    g_print ("  Type is '%s' but link failed.\n", newPadType);
    goto exit;
  } else {
    data->videoIsConnected = true;
    g_print ("  Link succeeded (type '%s').\n", newPadType);
  }

exit:
  // Unreference the new pad's caps, if we got them.
  if (newPadCaps != NULL)
  {
    gst_caps_unref (newPadCaps);
  }

  // Unreference the sink pad.
  if (sinkPad != NULL)
  {
    gst_object_unref (sinkPad);
  }
}

void MediaImpl::lockMutex()
{
  _mutexLocker->relock();
}

void MediaImpl::unlockMutex()
{
  _mutexLocker->unlock();
}

