/*
 * VideoImpl.h
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

#ifndef VIDEO_IMPL_H_
#define VIDEO_IMPL_H_

// GStreamer includes.
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/pbutils/pbutils.h>

// Other includes.
#include "MM.h"
#include <QtOpenGL>
#include <QMutex>
#include <QWaitCondition>

#include <glib.h>
#if __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

MM_BEGIN_NAMESPACE

/**
 * Private declaration of the video player.
 * This is to prevent the GStreamer header to be included in the whole project.
 * (it just needs to be included in this file).
 */
class VideoImpl
{
public:
  /**
   * Constructor.
   * This media player works for both video files and shared memory sockets.
   * If live is true, it's a shared memory socket.
   */
  VideoImpl(bool live=false);
  ~VideoImpl();

//  void setUri(const QString uri);
  /**
   * Returns whether or not GStreamer video support is ok.
   */
  static bool hasVideoSupport();

  /**
   * Sets up the player.
   * Basically calls loadMovie().
   */
  void build();

  /**
   * Returns the width of the video image.
   */
  int getWidth() const;

  /**
   * Returns the height of the video image.
   */
  int getHeight() const;

  /**
   * Returns the path to the media file being played.
   */
  QString getUri() const;

  /**
   * When using the shared memory source, returns whether or not we
   * are attached to a shared memory socket.
   */
  bool getAttached();

  /**
   * Returns the raw image of the last video frame.
   * It is currently unused!
   */
  const uchar* getBits();

  /// Returns true iff bits have started flowing (ie. if there is at least a first sample available).
  bool hasBits() const { return (_currentFrameSample != NULL); }

  /// Returns true iff bits have changed since last call to getBits().
  bool bitsHaveChanged() const { return _bitsChanged; }

  /**
   * Checks if the pipeline is ready.
   *
   * Returns whether or not the elements in the pipeline are connected,
   * and if we are using shmsrc, if the shared memory socket is being read.
   */
  bool isReady() const { return _isMovieReady() && videoIsConnected(); }

  bool videoIsConnected() const { return _videoIsConnected; }

  /**
   * Performs regular updates (checks if movie is ready and checks messages).
   */
  void update();

  // void runAudio();

  /**
   * Loads a new movie file.
   * 
   * Creates a new GStreamer pipeline, opens a movie or a shmsrc socket.
   */
  bool loadMovie(const QString& filename);

  bool setPlayState(bool play);
  bool getPlayState() const { return _playState; }

  bool seekIsEnabled() const { return _seekEnabled; }

  bool seekTo(double position);
  bool seekTo(guint64 positionNanoSeconds);

  /**
   * Tells the VideoImpl that we are actually reading from a shmsrc.
   * Called from the GStreamer callback of the shmsrc.
   */
  void setAttached(bool attach);

  void setRate(double rate=1.0);
  double getRate() const { return _rate; }

  void setVolume(double rate=0.0);
  double getVolume() const { return _volume; }

  void resetMovie();

protected:
  void unloadMovie();
  void freeResources();

private:
  /**
   * Checks if we reached the end of the video file.
   *
   * Returns false if the pipeline is not ready yet.
   */
  bool _eos() const;

  // void _finish();
  // void _init();

//  bool _preRun();
  void _checkMessages();
  void _setMovieReady(bool ready);
  bool _isMovieReady() const { return _movieReady; }
  void _setFinished(bool finished);

  // Sends the appropriate seek events to adjust to rate.
  void _updateRate();

  void _freeCurrentSample();

public:
  // GStreamer callback that simply sets the #newSample# flag to point to TRUE.
  static GstFlowReturn gstNewSampleCallback(GstElement*, VideoImpl *p);
  //static GstFlowReturn gstNewPreRollCallback (GstAppSink * appsink, gpointer user_data);

  // GStreamer callback that plugs the audio/video pads into the proper elements when they
  // are made available by the source.
  static void gstPadAddedCallback(GstElement *src, GstPad *newPad, VideoImpl* p);

  /// Locks mutex (default = no effect).
  void lockMutex();

  /// Unlocks mutex (default = no effect).
  void unlockMutex();

  /// Wait until first data samples are available (blocking).
  bool waitForNextBits(int timeout, const uchar** bits=0);

private:
  //locals

  // gstreamer elements
  GstBus *_bus;
  GstElement *_pipeline;
  GstElement *_uridecodebin0;
  GstElement *_shmsrc0;
  GstElement *_gdpdepay0;
  GstElement *_queue0;
  GstElement *_videoconvert0;
  GstElement *_appsink0;
  GstElement *_audioqueue0;
  GstElement *_audioconvert0;
  GstElement *_audioresample0;
  GstElement *_audiovolume0;
  GstElement *_audiosink0;

  /**
   * Temporary contains the image data of the last frame.
   */
  GstSample  *_currentFrameSample;
  GstBuffer  *_currentFrameBuffer;
  GstMapInfo  _mapInfo;
  bool       _bitsChanged;

  /**
   * Contains meta informations about current file.
   */
  int _width;
  int _height;
//  bool _isSeekable;
  guint64 _duration; // duration (in nanoseconds) (unused for now)

  bool _videoIsConnected;

  /**
   * shmsrc socket poller.
   */
  GSource *_pollSource;

  /// Raw image data of the last video frame.
  uchar *_data;

  /// Is seek enabled on the current pipeline?
  bool _seekEnabled;

  /// Playback rate (negative ==> reverse).
  double _rate;
  /// Audio playback volume (0.0 ==> 1.0).
  double _volume;

  /// Whether or not we are reading video from a shmsrc.
  bool _isSharedMemorySource;

  /// Whether or not we are attached to a shmsrc, if using a shmsrc.
  bool _attached;

  // unused
  bool _terminate;

  /// Is the movie (or rather pipeline) ready to play.
  bool _movieReady;

  /// Is the movie playing (as opposed to paused).
  bool _playState;

  /// Main mutex.
  QMutex _mutex;

  /// Main mutex locker (for the lockMutex() / unlockMutex() methods).
  QMutexLocker* _mutexLocker;

private:
  /**
   * Path of the movie file being played.
   */
  QString _uri;

  static const int MAX_SAMPLES_IN_BUFFER_QUEUES = 30;
};

MM_END_NAMESPACE

#endif /* ifndef */
