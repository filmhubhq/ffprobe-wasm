#include <vector>
#include <string>
#include <sstream>
#include <vector>
#include <inttypes.h>
#include <emscripten.h>
#include <emscripten/bind.h>

using namespace emscripten;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/bprint.h>
#include <libavutil/imgutils.h>
};

const std::string c_avformat_version() {
  return AV_STRINGIFY(LIBAVFORMAT_VERSION);
}

const std::string c_avcodec_version() {
  return AV_STRINGIFY(LIBAVCODEC_VERSION);
}

const std::string c_avutil_version() {
  return AV_STRINGIFY(LIBAVUTIL_VERSION);
}

typedef struct Tag {
  std::string key;
  std::string value;
} Tag;

typedef struct Stream {
  int id;
  float start_time;
  float duration;
  std::string codec_type;
  std::string codec_name;
  std::string format;
  float bit_rate;
  std::string profile;
  int level;
  int width;
  int height;
  int channels;
  int sample_rate;
  int frame_size;
  std::vector<Tag> tags;
  std::string r_frame_rate;
} Stream;

typedef struct Chapter {
  int id;
  std::string time_base;
  float start;
  float end;
  std::vector<Tag> tags;
} Chapter;

typedef struct FileInfoResponse {
  std::string name;
  float bit_rate;
  float duration;
  std::string url;
  int nb_streams;
  int flags;
  std::vector<Stream> streams;
  int nb_chapters;
  std::vector<Chapter> chapters;
  std::string error;
} FileInfoResponse;

FileInfoResponse get_file_info(std::string filename) {
  av_log_set_level(AV_LOG_QUIET); // No logging output for libav.

  // Initialize response struct with format data.
  FileInfoResponse r;

  FILE *file = fopen(filename.c_str(), "rb");
  if (!file) {
    r.error.assign("cannot open file\n");
    return r;
  }
  fclose(file);

  AVFormatContext *pFormatContext = avformat_alloc_context();
  if (!pFormatContext) {
    r.error.assign("could not allocate memory for Format Context");
    return r;
  }

  // Open the file and read header.
  int ret;
  if ((ret = avformat_open_input(&pFormatContext, filename.c_str(), NULL, NULL)) < 0) {
    r.error.assign(av_err2str(ret));
    return r;
  }

  // Get stream info from format.
  if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
    r.error.assign("could not get stream info");
    return r;
  }

  r.name = pFormatContext->iformat->name;
  r.bit_rate = (float)pFormatContext->bit_rate;
  r.duration = (float)pFormatContext->duration;
  r.url = pFormatContext->url;
  r.nb_streams = (int)pFormatContext->nb_streams;
  r.flags = pFormatContext->flags;
  r.nb_chapters = (int)pFormatContext->nb_chapters;

  // Loop through the streams.
  for (int i = 0; i < pFormatContext->nb_streams; i++) {
    AVCodecParameters *pLocalCodecParameters = NULL;
    pLocalCodecParameters = pFormatContext->streams[i]->codecpar;

    AVRational r_frame_rate = pFormatContext->streams[i]->r_frame_rate;

    std::stringstream r_frame_rate_str;
    r_frame_rate_str << (int)r_frame_rate.num << "/" << (int)r_frame_rate.den;

    Stream stream = {
      .id = (int)pFormatContext->streams[i]->id,
      .start_time = (float)pFormatContext->streams[i]->start_time,
      .duration = (float)pFormatContext->streams[i]->duration * (pFormatContext->streams[i]->time_base.num / (float) pFormatContext->streams[i]->time_base.den),
      .codec_type = av_get_media_type_string(pLocalCodecParameters->codec_type),
      .codec_name = avcodec_descriptor_get(pLocalCodecParameters->codec_id)->name,
      .format = av_get_pix_fmt_name((AVPixelFormat)pLocalCodecParameters->format),
      .bit_rate = (float)pLocalCodecParameters->bit_rate,
      .profile = avcodec_profile_name(pLocalCodecParameters->codec_id, pLocalCodecParameters->profile),
      .level = (int)pLocalCodecParameters->level,
      .width = (int)pLocalCodecParameters->width,
      .height = (int)pLocalCodecParameters->height,
      .channels = (int)pLocalCodecParameters->channels,
      .sample_rate = (int)pLocalCodecParameters->sample_rate,
      .frame_size = (int)pLocalCodecParameters->frame_size,
      .r_frame_rate = r_frame_rate_str.str(),
    };

    // Add tags to stream.
    const AVDictionaryEntry *tag = NULL;
    while ((tag = av_dict_get(pFormatContext->streams[i]->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
      Tag t = {
        .key = tag->key,
        .value = tag->value,
      };
      stream.tags.push_back(t);
    }

    r.streams.push_back(stream);
  }

  // Loop through the chapters (if any).
  for (int i = 0; i < pFormatContext->nb_chapters; i++) {
    AVChapter *chapter = pFormatContext->chapters[i];

    // Format timebase string to buf.
    AVBPrint buf;
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&buf, "%d%s%d", chapter->time_base.num, (char *)"/", chapter->time_base.den);

    Chapter c = {
      .id = (int)chapter->id,
      .time_base = buf.str,
      .start = (float)chapter->start,
      .end = (float)chapter->end,
    };

    // Add tags to chapter.
    const AVDictionaryEntry *tag = NULL;
    while ((tag = av_dict_get(chapter->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
      Tag t = {
        .key = tag->key,
        .value = tag->value,
      };
      c.tags.push_back(t);
    }

    r.chapters.push_back(c);
  }

  avformat_close_input(&pFormatContext);
  return r;
}

EMSCRIPTEN_BINDINGS(constants) {
  function("avformat_version", &c_avformat_version);
  function("avcodec_version", &c_avcodec_version);
  function("avutil_version", &c_avutil_version);
}

EMSCRIPTEN_BINDINGS(structs) {
  emscripten::value_object<Tag>("Tag")
  .field("key", &Tag::key)
  .field("value", &Tag::value)
  ;
  register_vector<Tag>("Tag");

  emscripten::value_object<Stream>("Stream")
  .field("id", &Stream::id)
  .field("start_time", &Stream::start_time)
  .field("duration", &Stream::duration)
  .field("codec_type", &Stream::codec_type)
  .field("codec_name", &Stream::codec_name)
  .field("format", &Stream::format)
  .field("bit_rate", &Stream::bit_rate)
  .field("profile", &Stream::profile)
  .field("level", &Stream::level)
  .field("width", &Stream::width)
  .field("height", &Stream::height)
  .field("channels", &Stream::channels)
  .field("sample_rate", &Stream::sample_rate)
  .field("frame_size", &Stream::frame_size)
  .field("tags", &Stream::tags)
  .field("r_frame_rate", &Stream::r_frame_rate)
  ;
  register_vector<Stream>("Stream");

  emscripten::value_object<Chapter>("Chapter")
  .field("id", &Chapter::id)
  .field("time_base", &Chapter::time_base)
  .field("start", &Chapter::start)
  .field("end", &Chapter::end)
  .field("tags", &Chapter::tags)
  ;
  register_vector<Chapter>("Chapter");

  emscripten::value_object<FileInfoResponse>("FileInfoResponse")
  .field("name", &FileInfoResponse::name)
  .field("duration", &FileInfoResponse::duration)
  .field("bit_rate", &FileInfoResponse::bit_rate)
  .field("url", &FileInfoResponse::url)
  .field("nb_streams", &FileInfoResponse::nb_streams)
  .field("flags", &FileInfoResponse::flags)
  .field("streams", &FileInfoResponse::streams)
  .field("nb_chapters", &FileInfoResponse::nb_chapters)
  .field("chapters", &FileInfoResponse::chapters)
  .field("error", &FileInfoResponse::error)
  ;
  function("get_file_info", &get_file_info);

}