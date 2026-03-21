/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS.md)
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

#include "include/headless.hpp"

#include "include/config.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
#include "include/theme.hpp"
#include "include/visualizer_registry.hpp"
#include "include/window_manager.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace Headless {

namespace Resampler {
SwrContext* swr_ctx = nullptr;
SwrContext* swr_video_ctx = nullptr;
int chMap[5] = {0, 1, -1, -1, -1};

void init(AVCodecContext* codec_ctx, AVCodecContext* audio_enc_ctx) {
  // swr_ctx: input file sample rate/format -> FLTP, input sample rate
  if (swr_alloc_set_opts2(&swr_ctx, &codec_ctx->ch_layout, AV_SAMPLE_FMT_FLTP, codec_ctx->sample_rate,
                          &codec_ctx->ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate, 0, NULL) < 0)
    throw makeErrorAt(std::source_location::current(), "Could not setup swresample");
  if (swr_init(swr_ctx) < 0)
    throw makeErrorAt(std::source_location::current(), "Could not init swr");
  // swr_video_ctx: input sample rate/format -> FLTP, 44100
  if (swr_alloc_set_opts2(&swr_video_ctx, &audio_enc_ctx->ch_layout, AV_SAMPLE_FMT_FLTP, 44100, &codec_ctx->ch_layout,
                          AV_SAMPLE_FMT_FLTP, codec_ctx->sample_rate, 0, NULL) < 0)
    throw makeErrorAt(std::source_location::current(), "Could not setup swresample for video output");
  if (codec_ctx->ch_layout.nb_channels == 5)
    if (swr_set_channel_mapping(swr_video_ctx, chMap) < 0)
      throw makeErrorAt(std::source_location::current(), "Failed to set channel mapping for video output");
  if (swr_init(swr_video_ctx) < 0)
    throw makeErrorAt(std::source_location::current(), "Could not init swr for video output");
}

void cleanup() {
  if (swr_ctx)
    swr_free(&swr_ctx);
  if (swr_video_ctx)
    swr_free(&swr_video_ctx);
}
} // namespace Resampler

namespace AudioReader {
AVFormatContext* fmt_ctx = nullptr;
AVCodecContext* codec_ctx = nullptr;
int channels = 0;

void init() {
  if (avformat_open_input(&fmt_ctx, CmdlineArgs::file.c_str(), nullptr, nullptr) < 0)
    throw makeErrorAt(std::source_location::current(), "Could not open file at {}", CmdlineArgs::file);

  if (avformat_find_stream_info(fmt_ctx, nullptr) < 0)
    throw makeErrorAt(std::source_location::current(), "Could not get params");
  AVCodecParameters* codecpar = fmt_ctx->streams[0]->codecpar;
  const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
  if (!codec)
    throw makeErrorAt(std::source_location::current(), "Could not get codec");
  codec_ctx = avcodec_alloc_context3(codec);
  if (!codec_ctx)
    throw makeErrorAt(std::source_location::current(), "Could not get codec context");
  avcodec_parameters_to_context(codec_ctx, codecpar);
  avcodec_open2(codec_ctx, codec, nullptr);
  Config::options.audio.sample_rate = codec_ctx->sample_rate;
  channels = codec_ctx->ch_layout.nb_channels;
  av_channel_layout_default(&codec_ctx->ch_layout, channels);
  codec_ctx->ch_layout.order = AV_CHANNEL_ORDER_NATIVE;
}
void cleanup() {
  if (codec_ctx)
    avcodec_free_context(&codec_ctx);
  if (fmt_ctx)
    avformat_close_input(&fmt_ctx);
}
} // namespace AudioReader

namespace VideoEncoder {
AVFormatContext* out_fmt_ctx = nullptr;
AVStream* video_st = nullptr;
AVStream* audio_st = nullptr;
AVCodecContext* video_enc_ctx = nullptr;
AVCodecContext* audio_enc_ctx = nullptr;
SwsContext* sws_ctx = nullptr;
int fps = 0;
int64_t video_pts = 0;
int64_t audio_pts = 0;
void init() {
  fps = Config::options.window.fps_limit;
  avformat_alloc_output_context2(&out_fmt_ctx, nullptr, nullptr, CmdlineArgs::outFile.c_str());
  if (!out_fmt_ctx)
    throw makeErrorAt(std::source_location::current(), "Could not allocate output context");
  // Video stream
  const AVCodec* video_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (!video_codec)
    throw makeErrorAt(std::source_location::current(), "Could not find H264 encoder");
  video_st = avformat_new_stream(out_fmt_ctx, video_codec);
  if (!video_st)
    throw makeErrorAt(std::source_location::current(), "Could not create video stream");
  video_enc_ctx = avcodec_alloc_context3(video_codec);
  video_enc_ctx->codec_id = AV_CODEC_ID_H264;
  video_enc_ctx->width = CmdlineArgs::resolution;
  video_enc_ctx->height = CmdlineArgs::resolution;
  video_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  video_enc_ctx->time_base = {1, fps};
  video_enc_ctx->framerate = {fps, 1};
  video_enc_ctx->gop_size = 12;
  video_enc_ctx->max_b_frames = 2;
  video_enc_ctx->bit_rate = 20 * 1000 * 1000;
  if (out_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    video_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  avcodec_open2(video_enc_ctx, video_codec, nullptr);
  avcodec_parameters_from_context(video_st->codecpar, video_enc_ctx);
  video_st->time_base = video_enc_ctx->time_base;
  // Audio stream
  const AVCodec* audio_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (!audio_codec)
    throw makeErrorAt(std::source_location::current(), "Could not find AAC encoder");
  audio_st = avformat_new_stream(out_fmt_ctx, audio_codec);
  if (!audio_st)
    throw makeErrorAt(std::source_location::current(), "Could not create audio stream");
  audio_enc_ctx = avcodec_alloc_context3(audio_codec);
  audio_enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
  audio_enc_ctx->sample_rate = 44100;
  av_channel_layout_default(&audio_enc_ctx->ch_layout, 2); // stereo
  audio_enc_ctx->bit_rate = 256000;
  audio_enc_ctx->time_base = {1, audio_enc_ctx->sample_rate};
  if (out_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    audio_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  avcodec_open2(audio_enc_ctx, audio_codec, nullptr);
  avcodec_parameters_from_context(audio_st->codecpar, audio_enc_ctx);
  audio_st->time_base = audio_enc_ctx->time_base;
  if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE))
    if (avio_open(&out_fmt_ctx->pb, CmdlineArgs::outFile.c_str(), AVIO_FLAG_WRITE) < 0)
      throw makeErrorAt(std::source_location::current(), "Could not open output file");
  if (avformat_write_header(out_fmt_ctx, nullptr) < 0)
    throw makeErrorAt(std::source_location::current(), "Could not write header");
}

void cleanup() {
  if (video_enc_ctx)
    avcodec_free_context(&video_enc_ctx);
  if (audio_enc_ctx)
    avcodec_free_context(&audio_enc_ctx);
  if (sws_ctx)
    sws_freeContext(sws_ctx);
  if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE))
    avio_closep(&out_fmt_ctx->pb);
  if (out_fmt_ctx)
    avformat_free_context(out_fmt_ctx);
}
} // namespace VideoEncoder

bool hasRGBCh = false;
std::chrono::milliseconds frameDelay = 1ms;

void render() {
  using namespace Headless;
  // Suppress FFmpeg/libx264/aac logs unless debug is enabled
  if (!CmdlineArgs::debug) {
    av_log_set_level(AV_LOG_ERROR);
  } else {
    av_log_set_level(AV_LOG_INFO);
  }
  try {
    VideoEncoder::init();
    AudioReader::init();
    Resampler::init(AudioReader::codec_ctx, VideoEncoder::audio_enc_ctx);
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    // Cleanup any partial state
    Resampler::cleanup();
    AudioReader::cleanup();
    VideoEncoder::cleanup();
    return;
  }

  if (AudioReader::channels != 2 && AudioReader::channels != 5) {
    logWarnAt(std::source_location::current(), "Incompatible channel count");
    Resampler::cleanup();
    AudioReader::cleanup();
    VideoEncoder::cleanup();
    return;
  }
  if (AudioReader::channels == 5)
    hasRGBCh = true;

  auto vectorScope = VisualizerRegistry::find("lissajous");
  vectorScope->bounds = WindowManager::Bounds {0, 0, CmdlineArgs::resolution, CmdlineArgs::resolution};
  vectorScope->drawBounds = WindowManager::Bounds {0, 0, 512, 512};
  vectorScope->resizeTextures();

  DSP::bufferMid.resize(DSP::bufferSize);
  DSP::bufferSide.resize(DSP::bufferSize);
  DSP::bufferR.resize(DSP::bufferSize);
  DSP::bufferG.resize(DSP::bufferSize);
  DSP::bufferB.resize(DSP::bufferSize);

  if (CmdlineArgs::limitFps)
    frameDelay = 1000ms / Config::options.window.fps_limit;

  double audio_samples_per_frame = (double)AudioReader::codec_ctx->sample_rate / (double)VideoEncoder::fps;
  double audio_frame_accum = 0.0;

  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  GLuint pbos[2] = {0, 0};
  glGenBuffers(2, pbos);
  int pboIndex = 0;

#ifndef _WIN32
  sigset_t sigset;
  if (!CmdlineArgs::debug) {
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGWINCH);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGQUIT);
    sigprocmask(SIG_BLOCK, &sigset, nullptr);
  }
#endif

  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  AVFrame* video_frame = av_frame_alloc();
  video_frame->format = AV_PIX_FMT_YUV420P;
  video_frame->width = CmdlineArgs::resolution;
  video_frame->height = CmdlineArgs::resolution;
  av_frame_get_buffer(video_frame, 32);
  AVPacket* out_pkt = av_packet_alloc();

  // Preallocate audio buffer
  std::vector<std::vector<float>> audio_buffer(VideoEncoder::audio_enc_ctx->ch_layout.nb_channels);
  int buffered_samples = 0;
  int channels = VideoEncoder::audio_enc_ctx->ch_layout.nb_channels;
  int frame_size = VideoEncoder::audio_enc_ctx->frame_size;
  // Preallocate sws_ctx
  if (!VideoEncoder::sws_ctx) {
    VideoEncoder::sws_ctx =
        sws_getContext(CmdlineArgs::resolution, CmdlineArgs::resolution, AV_PIX_FMT_RGB24, CmdlineArgs::resolution,
                       CmdlineArgs::resolution, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
  }
  std::vector<uint8_t> pixels(CmdlineArgs::resolution * CmdlineArgs::resolution * 3);

  while (av_read_frame(AudioReader::fmt_ctx, pkt) >= 0) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      SDLWindow::handleEvent(event);
    }
#ifndef _WIN32
    struct timespec ts = {0, 0};
    int sig = sigtimedwait(&sigset, nullptr, &ts);
    if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT)
      SDLWindow::running = false;
#endif
    if (!SDLWindow::running)
      break;
    SDLWindow::selectWindow("main");
    avcodec_send_packet(AudioReader::codec_ctx, pkt);
    while (avcodec_receive_frame(AudioReader::codec_ctx, frame) == 0) {
      auto renderAndEncodeFn = [&]() {
        auto frameStart = std::chrono::steady_clock::now();
        WindowManager::setViewport(vectorScope->drawBounds);
        SDLWindow::clear();
        vectorScope->render();
        // Draw progress bar
        double progress = 0.0;
        if (AudioReader::fmt_ctx && AudioReader::fmt_ctx->duration > 0) {
          int64_t pos = 0;
          if (pkt && pkt->pts != AV_NOPTS_VALUE) {
            pos = av_rescale_q(pkt->pts, AudioReader::fmt_ctx->streams[pkt->stream_index]->time_base, AV_TIME_BASE_Q);
          } else if (frame && frame->pts != AV_NOPTS_VALUE) {
            pos = av_rescale_q(frame->pts, AudioReader::fmt_ctx->streams[0]->time_base, AV_TIME_BASE_Q);
          }
          progress = std::clamp((double)pos / (double)AudioReader::fmt_ctx->duration, 0.0, 1.0);
        }
        float barHeight = 12.0f;
        float barMargin = 8.0f;
        float fullBarWidth = (float)512 - 2 * barMargin;
        Graphics::drawFilledRect(barMargin, barMargin, fullBarWidth, barHeight, Theme::colors.background);
        Graphics::drawFilledRect(barMargin, barMargin, fullBarWidth * progress, barHeight, Theme::colors.color);
        SDLWindow::display();
        // FBO/PBO readback
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vectorScope->phosphor.outputTexture,
                               0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
          logWarnAt(std::source_location::current(), "FBO is not complete for outputTexture readback");
        }
        int width = CmdlineArgs::resolution;
        int height = CmdlineArgs::resolution;
        size_t pixelBytes = width * height * 3;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos[pboIndex]);
        glBufferData(GL_PIXEL_PACK_BUFFER, pixelBytes, nullptr, GL_STREAM_READ);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        int nextPbo = (pboIndex + 1) % 2;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos[nextPbo]);
        uint8_t* pboPtr = (uint8_t*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (pboPtr) {
          memcpy(pixels.data(), pboPtr, pixelBytes);
          glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        pboIndex = nextPbo;
        // Convert to YUV420P for encoder
        const uint8_t* inData[1] = {pixels.data()};
        int inLinesize[1] = {3 * CmdlineArgs::resolution};
        sws_scale(VideoEncoder::sws_ctx, inData, inLinesize, 0, CmdlineArgs::resolution, video_frame->data,
                  video_frame->linesize);
        video_frame->pts = VideoEncoder::video_pts++;
        int ret = avcodec_send_frame(VideoEncoder::video_enc_ctx, video_frame);
        while (ret >= 0) {
          ret = avcodec_receive_packet(VideoEncoder::video_enc_ctx, out_pkt);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
          if (ret < 0)
            break;
          out_pkt->stream_index = VideoEncoder::video_st->index;
          av_packet_rescale_ts(out_pkt, VideoEncoder::video_enc_ctx->time_base, VideoEncoder::video_st->time_base);
          av_interleaved_write_frame(VideoEncoder::out_fmt_ctx, out_pkt);
          av_packet_unref(out_pkt);
        }
        if (CmdlineArgs::limitFps) {
          auto frameEnd = std::chrono::steady_clock::now();
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart);
          if (elapsed < frameDelay) {
            std::this_thread::sleep_for(frameDelay - elapsed);
          }
        }
      };

      AVFrame* swr_frame = av_frame_alloc();
      swr_frame->ch_layout = AudioReader::codec_ctx->ch_layout;
      swr_frame->format = AV_SAMPLE_FMT_FLTP;
      swr_frame->sample_rate = AudioReader::codec_ctx->sample_rate;
      swr_frame->nb_samples = frame->nb_samples;
      if (av_frame_get_buffer(swr_frame, 0) < 0) {
        logWarnAt(std::source_location::current(), "av_frame_get_buffer failed for swr_frame");
        av_frame_free(&swr_frame);
        break;
      }
      int swRet = swr_convert_frame(Resampler::swr_ctx, swr_frame, frame);
      if (swRet < 0) {
        char errbuf[256];
        av_strerror(swRet, errbuf, sizeof(errbuf));
        logWarnAt(std::source_location::current(), "swr_convert_frame failed: {}", errbuf);
        av_frame_free(&swr_frame);
        break;
      }
      AVFrame* resampled_frame = av_frame_alloc();
      resampled_frame->ch_layout = VideoEncoder::audio_enc_ctx->ch_layout;
      resampled_frame->format = AV_SAMPLE_FMT_FLTP;
      resampled_frame->sample_rate = 44100;
      resampled_frame->nb_samples = av_rescale_rnd(swr_frame->nb_samples, 44100, swr_frame->sample_rate, AV_ROUND_UP);
      if (av_frame_get_buffer(resampled_frame, 0) < 0) {
        logWarnAt(std::source_location::current(), "av_frame_get_buffer failed for resampled_frame");
        av_frame_free(&resampled_frame);
        break;
      }
      int resample_ret = swr_convert_frame(Resampler::swr_video_ctx, resampled_frame, swr_frame);
      if (resample_ret < 0) {
        char errbuf[256];
        av_strerror(resample_ret, errbuf, sizeof(errbuf));
        logWarnAt(std::source_location::current(), "swr_convert_frame (video) failed: {}", errbuf);
        av_frame_free(&resampled_frame);
        break;
      }
      for (int ch = 0; ch < channels; ++ch) {
        float* src = (float*)resampled_frame->extended_data[ch];
        audio_buffer[ch].insert(audio_buffer[ch].end(), src, src + resampled_frame->nb_samples);
      }
      buffered_samples += resampled_frame->nb_samples;
      while (buffered_samples >= frame_size) {
        AVFrame* audio_frame = av_frame_alloc();
        audio_frame->nb_samples = frame_size;
        audio_frame->format = VideoEncoder::audio_enc_ctx->sample_fmt;
        audio_frame->sample_rate = VideoEncoder::audio_enc_ctx->sample_rate;
        audio_frame->ch_layout = VideoEncoder::audio_enc_ctx->ch_layout;
        av_frame_get_buffer(audio_frame, 0);
        for (int ch = 0; ch < channels; ++ch) {
          memcpy(audio_frame->extended_data[ch], audio_buffer[ch].data(), sizeof(float) * frame_size);
        }
        audio_frame->pts = VideoEncoder::audio_pts;
        VideoEncoder::audio_pts += frame_size;
        AVPacket* audio_pkt = av_packet_alloc();
        int aret = avcodec_send_frame(VideoEncoder::audio_enc_ctx, audio_frame);
        while (aret >= 0) {
          aret = avcodec_receive_packet(VideoEncoder::audio_enc_ctx, audio_pkt);
          if (aret == AVERROR(EAGAIN) || aret == AVERROR_EOF)
            break;
          if (aret < 0)
            break;
          audio_pkt->stream_index = VideoEncoder::audio_st->index;
          av_packet_rescale_ts(audio_pkt, VideoEncoder::audio_enc_ctx->time_base, VideoEncoder::audio_st->time_base);
          av_interleaved_write_frame(VideoEncoder::out_fmt_ctx, audio_pkt);
          av_packet_unref(audio_pkt);
        }
        av_packet_free(&audio_pkt);
        av_frame_free(&audio_frame);
        for (int ch = 0; ch < channels; ++ch) {
          audio_buffer[ch].erase(audio_buffer[ch].begin(), audio_buffer[ch].begin() + frame_size);
        }
        buffered_samples -= frame_size;
      }
      av_frame_free(&resampled_frame);
      float** planes = (float**)swr_frame->data;
      int n = swr_frame->nb_samples;
      if (swr_frame->ch_layout.nb_channels == 2) {
        for (int i = 0; i < n; i++) {
          float L = planes[0][i];
          float R = -planes[1][i];
          DSP::bufferMid[DSP::writePos] = (L + R) * 0.5f;
          DSP::bufferSide[DSP::writePos] = (L - R) * 0.5f;
          DSP::writePos = (DSP::writePos + 1) % DSP::bufferSize;
          audio_frame_accum += 1.0;
          if (audio_frame_accum >= audio_samples_per_frame) {
            renderAndEncodeFn();
            audio_frame_accum -= audio_samples_per_frame;
          }
        }
      } else if (swr_frame->ch_layout.nb_channels == 5) {
        for (int i = 0; i < n; i++) {
          float L = planes[0][i];
          float R = -planes[1][i];
          DSP::bufferMid[DSP::writePos] = (L + R) * 0.5f;
          DSP::bufferSide[DSP::writePos] = (L - R) * 0.5f;
          DSP::bufferR[DSP::writePos] = planes[2][i] * 0.5f + 0.5f;
          DSP::bufferG[DSP::writePos] = planes[3][i] * 0.5f + 0.5f;
          DSP::bufferB[DSP::writePos] = planes[4][i] * 0.5f + 0.5f;
          DSP::writePos = (DSP::writePos + 1) % DSP::bufferSize;
          audio_frame_accum += 1.0;
          if (audio_frame_accum >= audio_samples_per_frame) {
            renderAndEncodeFn();
            audio_frame_accum -= audio_samples_per_frame;
          }
        }
      }
      av_frame_free(&swr_frame);
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  av_frame_free(&frame);
  av_frame_free(&video_frame);
  av_packet_free(&out_pkt);
  glDeleteBuffers(2, pbos);
  av_write_trailer(VideoEncoder::out_fmt_ctx);

  Resampler::cleanup();
  AudioReader::cleanup();
  VideoEncoder::cleanup();
  glDeleteFramebuffers(1, &fbo);
}

} // namespace Headless
