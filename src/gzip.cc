/*
 * Copyright 2009, Acknack Ltd. All rights reserved.
 * Copyright 2010, Ivan Egorov (egorich.3.04@gmail.com).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <node.h>
#include <node_events.h>
#include <node_buffer.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>

#include "utils.h"
#include "zlib.h"

using namespace v8;
using namespace node;

class GzipUtils {
 public:
  typedef ScopedOutputBuffer<Bytef> Blob;

 public:
  static int StatusOk() {
    return Z_OK;
  }


  static int StatusSequenceError() {
    return Z_STREAM_ERROR;
  }


  static int StatusMemoryError() {
    return Z_MEM_ERROR;
  }


  static int StatusEndOfStream() {
    return Z_STREAM_END;
  }

 public:
  static bool IsError(int gzipStatus) {
    return !(gzipStatus == Z_OK || gzipStatus == Z_STREAM_END);
  }


  static Local<Value> GetException(int gzipStatus) {
    if (!IsError(gzipStatus)) {
      return Local<Value>::New(Undefined());
    } else {
      switch (gzipStatus) {
        case Z_NEED_DICT: 
          return Exception::Error(String::New(NeedDictionary));
        case Z_ERRNO: 
          return Exception::Error(String::New(Errno));
        case Z_STREAM_ERROR: 
          return Exception::Error(String::New(StreamError));
        case Z_DATA_ERROR: 
          return Exception::Error(String::New(DataError));
        case Z_MEM_ERROR: 
          return Exception::Error(String::New(MemError));
        case Z_BUF_ERROR: 
          return Exception::Error(String::New(BufError));
        case Z_VERSION_ERROR: 
          return Exception::Error(String::New(VersionError));

        default:
          return Exception::Error(String::New("Unknown error"));
      }
    }
  }

 private:
  static const char NeedDictionary[];
  static const char Errno[];
  static const char StreamError[];
  static const char DataError[];
  static const char MemError[];
  static const char BufError[];
  static const char VersionError[];
};
const char GzipUtils::NeedDictionary[] = "Dictionary must be specified. "
  "Currently this is unsupported by library.";
const char GzipUtils::Errno[] = "Z_ERRNO: Input/output error.";
const char GzipUtils::StreamError[] = "Z_STREAM_ERROR: Invalid arguments or "
  "stream state is inconsistent.";
const char GzipUtils::DataError[] = "Z_DATA_ERROR: Input data corrupted.";
const char GzipUtils::MemError[] = "Z_MEM_ERROR: Out of memory.";
const char GzipUtils::BufError[] = "Z_BUF_ERROR: Buffer error.";
const char GzipUtils::VersionError[] = "Z_VERSION_ERROR: "
  "Invalid library version.";


class GzipImpl {
  friend class ZipLib<GzipImpl>;

  typedef GzipUtils Utils;
  typedef GzipUtils::Blob Blob;

 private:
  static const char Name[];

 private:
  Handle<Value> Init(const Arguments &args) {
    HandleScope scope;

    int level = Z_DEFAULT_COMPRESSION;
    if (args.Length() > 0 && !args[0]->IsUndefined()) {
      if (!args[0]->IsInt32()) {
        Local<Value> exception = Exception::TypeError(
            String::New("level must be an integer"));
        return ThrowException(exception);
      }
      level = args[0]->Int32Value();
    }

    stream_.zalloc = Z_NULL;
    stream_.zfree = Z_NULL;
    stream_.opaque = Z_NULL;

    int ret = deflateInit2(&stream_, level,
                           Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    if (Utils::IsError(ret)) {
      return ThrowException(Utils::GetException(ret));
    }
    return Undefined();
  }


  int Write(char *data, int &dataLength, Blob &out) {
    stream_.next_in = reinterpret_cast<Bytef*>(data);
    stream_.avail_in = dataLength;
    stream_.next_out = out.data() + out.length();
    size_t initAvail = stream_.avail_out = out.avail();

    int ret = deflate(&stream_, Z_NO_FLUSH);
    dataLength = stream_.avail_in;
    if (!Utils::IsError(ret)) {
      out.IncreaseLengthBy(initAvail - stream_.avail_out);
    }
    return ret;
  }


  int Finish(Blob &out) {
    stream_.avail_in = 0;
    stream_.next_in = NULL;
    stream_.next_out = out.data() + out.length();
    int initAvail = stream_.avail_out = out.avail();

    int ret = deflate(&stream_, Z_FINISH);
    if (!Utils::IsError(ret)) {
      out.IncreaseLengthBy(initAvail - stream_.avail_out);
    }
    return ret;
  }


  void Destroy() {
    deflateEnd(&stream_);
  }

 private:
  z_stream stream_;
};
const char GzipImpl::Name[] = "Gzip";
typedef ZipLib<GzipImpl> Gzip;


class GunzipImpl {
  friend class ZipLib<GunzipImpl>;

  typedef GzipUtils Utils;
  typedef GzipUtils::Blob Blob;

 private:
  static const char Name[];

 private:
  Handle<Value> Init(const Arguments &args) {
    stream_.zalloc = Z_NULL;
    stream_.zfree = Z_NULL;
    stream_.opaque = Z_NULL;
    stream_.avail_in = 0;
    stream_.next_in = Z_NULL;

    int ret = inflateInit2(&stream_, MAX_WBITS);
    if (Utils::IsError(ret)) {
      return ThrowException(Utils::GetException(ret));
    }
    return Undefined();
  }


  int Write(char* data, int &dataLength, Blob &out) {
    stream_.next_in = reinterpret_cast<Bytef*>(data);
    stream_.avail_in = dataLength;
    stream_.next_out = out.data() + out.length();
    size_t initAvail = stream_.avail_out = out.avail();

    int ret = inflate(&stream_, Z_NO_FLUSH);
    dataLength = stream_.avail_in;
    if (!Utils::IsError(ret)) {
      out.IncreaseLengthBy(initAvail - stream_.avail_out);
    }
    return ret;
  }


  int Finish(Blob &out) {
    return Z_OK;
  }


  void Destroy() {
    inflateEnd(&stream_);
  }

 private:
  z_stream stream_;
};
const char GunzipImpl::Name[] = "Gunzip";
typedef ZipLib<GunzipImpl> Gunzip;

