/*
Copyright (c) 2014, Martin Sjölund
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <msgpack.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <ModelicaUtilities.h>


typedef struct {
  msgpack_unpacked msg;
  int fd;
  void *ptr;
  size_t size;
} s_deserializer;

typedef struct {
  FILE *fout;
  char *str;
  size_t size;
} s_stringstream;

int msgpack_modelica_pack_map(void *packer, int len)
{
  return msgpack_pack_map(packer,len);
}

void* msgpack_modelica_packer_new_sbuffer(void *sbuffer)
{
  return msgpack_packer_new(sbuffer, msgpack_sbuffer_write);
}

int msgpack_modelica_pack_array(void *packer, int len)
{
  return msgpack_pack_array(packer,len);
}

int msgpack_modelica_pack_string(void* packer, const char *str)
{
  size_t len = strlen(str);
  if (msgpack_pack_raw(packer,len)) {
    return 1;
  }
  return msgpack_pack_raw_body(packer,str,len);
}

void omc_sbuffer_to_file(void *ptr, const char *file)
{
  msgpack_sbuffer* buffer = (msgpack_sbuffer*) ptr;
  FILE *fout = fopen(file, "w");
  if (!fout) {
    ModelicaFormatError("Failed to open file %s for writing", file);
  }
  if (1 != fwrite(buffer->data, buffer->size, 1, fout)) {
    ModelicaFormatError("Failed to write to file %s", file);
  }
}

static void unpack_print(FILE *fout, const void *ptr, size_t size) {
  size_t off = 0;
  msgpack_unpacked msg;
  msgpack_unpacked_init(&msg);
  while (msgpack_unpack_next(&msg, ptr, size, &off)) {
    msgpack_object root = msg.data;
    msgpack_object_print(fout,root);
    fputc('\n',fout);
  }
  msgpack_unpacked_destroy(&msg);
}

const char* msgpack_modelica_deserialize(const char *file)
{
  int fd;
  char *str,*res;
  void *mapped;
  size_t sz_str = 0;
  FILE *fout = open_memstream(&str,&sz_str);
  struct stat s;
  fd = open(file, O_RDONLY);
  if (fd < 0) {
    ModelicaFormatError("Failed to open file %s for reading: %s\n", file, strerror(errno));
  }
  if (fstat(fd, &s) < 0) {
    ModelicaFormatError("stat %s failed: %s\n", file, strerror(errno));
  }
  mapped = mmap(0, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (mapped == MAP_FAILED) {
    ModelicaFormatError("mmap(%s) failed: %s\n", file, strerror(errno));
  }
  unpack_print(fout, mapped, s.st_size);
  munmap(mapped, s.st_size);
  close(fd);
  fclose(fout);
  res=ModelicaAllocateString(sz_str);
  memcpy(res,str,sz_str);
  free(str);
  return res;
}

void* msgpack_modelica_new_deserialiser(const char *file)
{
  s_deserializer *deserializer = (s_deserializer*) malloc(sizeof(s_deserializer));
  int fd;
  void *mapped;
  struct stat s;
  fd = open(file, O_RDONLY);
  if (fd < 0) {
    ModelicaFormatError("Failed to open file %s for reading: %s\n", file, strerror(errno));
  }
  if (fstat(fd, &s) < 0) {
    close(fd);
    ModelicaFormatError("fstat %s failed: %s\n", file, strerror(errno));
  }
  mapped = mmap(0, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (mapped == MAP_FAILED) {
    close(fd);
    ModelicaFormatError("mmap(%s) failed: %s\n", file, strerror(errno));
  }
  msgpack_unpacked_init(&deserializer->msg);
  deserializer->fd = fd;
  deserializer->ptr = mapped;
  deserializer->size = s.st_size;
  return deserializer;
}

void msgpack_modelica_free_deserialiser(void *ptr)
{
  s_deserializer *deserializer = (s_deserializer*) ptr;
  msgpack_unpacked_destroy(&deserializer->msg);
  munmap(deserializer->ptr, deserializer->size);
  close(deserializer->fd);
  free(ptr);
}

int msgpack_modelica_unpack_next(void *ptr, int offset, int *newoffset)
{
  s_deserializer *deserializer = (s_deserializer*) ptr;
  size_t off = offset;
  int res = msgpack_unpack_next(&deserializer->msg, deserializer->ptr, deserializer->size, &off);
  *newoffset = off;
  return res;
}

int msgpack_modelica_unpack_int(void *ptr, int offset, int *newoffset, int *success)
{
  s_deserializer *deserializer = (s_deserializer*) ptr;
  size_t off = offset;
  *success = msgpack_unpack_next(&deserializer->msg, deserializer->ptr, deserializer->size, &off);
  if (!*success) {
    ModelicaError("Failed to unpack object\n");
  }
  *newoffset = off;
  if (deserializer->msg.data.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
    return deserializer->msg.data.via.u64;
  } else if (deserializer->msg.data.type == MSGPACK_OBJECT_NEGATIVE_INTEGER) {
    return deserializer->msg.data.via.i64;
  } else {
    ModelicaError("Object is not of integer type\n");
  }
  return 0; /* Cannot reach here */
}

const char* msgpack_modelica_unpack_string(void *ptr, int offset, int *newoffset, int *success)
{
  s_deserializer *deserializer = (s_deserializer*) ptr;
  size_t off = offset;
  *success = msgpack_unpack_next(&deserializer->msg, deserializer->ptr, deserializer->size, &off);
  if (!*success) {
    ModelicaError("Failed to unpack object\n");
  }
  *newoffset = off;
  if (deserializer->msg.data.type == MSGPACK_OBJECT_RAW) {
    size_t sz = deserializer->msg.data.via.raw.size;
    char *res = ModelicaAllocateString(sz);
    memcpy(res, deserializer->msg.data.via.raw.ptr, sz);
    return res;
  } else {
    ModelicaError("Object is not of integer type\n");
  }
  return NULL; /* Cannot reach here */
}

int msgpack_modelica_get_unpacked_int(void *ptr)
{
  s_deserializer *deserializer = (s_deserializer*) ptr;
  return deserializer->msg.data.via.i64;
}

int msgpack_modelica_unpack_any_to_stringstream(void *ptr1, void *ptr2, int offset, int *newoffset)
{
  s_deserializer *deserializer = (s_deserializer*) ptr1;
  s_stringstream *st = (s_stringstream *) ptr2;
  size_t off = offset;
  if (msgpack_unpack_next(&deserializer->msg, deserializer->ptr, deserializer->size, &off)) {
    msgpack_object root = deserializer->msg.data;
    msgpack_object_print(st->fout,root);
    *newoffset = off;
    return 1;
  } else {
    *newoffset = off;
    return 0;
  }
}

void* msgpack_modelica_new_stringstream()
{
  s_stringstream *st = (s_stringstream *) malloc(sizeof(s_stringstream));
  st->str = 0;
  st->size = 0;
  st->fout = open_memstream(&st->str,&st->size);
  return st;
}

void msgpack_modelica_free_stringstream(void *ptr)
{
  s_stringstream *st = (s_stringstream *) ptr;
  fclose(st->fout);
  free(st->str);
  free(st);
}

char* msgpack_modelica_stringstream_get(void *ptr)
{
  char *res;
  s_stringstream *st = (s_stringstream *) ptr;
  fclose(st->fout);
  res = ModelicaAllocateStringWithErrorReturn(st->size);
  memcpy(res,st->str,st->size);
  free(st->str);
  if (!res) {
    ModelicaError("Failed to allocate memory for stringstream\n");
  }
  st->str = 0;
  st->size = 0;
  st->fout = open_memstream(&st->str,&st->size);
  return res;
}

void msgpack_modelica_stringstream_append(void *ptr, const char *str)
{
  s_stringstream *st = (s_stringstream *) ptr;
  fputs(str, st->fout);
}
