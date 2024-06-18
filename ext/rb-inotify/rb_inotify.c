#include <ruby.h>

#ifndef HAVE_SYS_INOTIFY_H

RUBY_FUNC_EXPORTED void
Init_rb_inotify(void)
{
  fprintf(stderr, "no inotify!"); // TODO: do we need anything here?
} 

#else // HAVE_SYS_INOTIFY_H

#include <ruby/thread.h>
#include <ruby/util.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <linux/limits.h>

typedef struct {
  int fd;
  st_table *watchers;
} rb_inotify_handle;

static int
inotify_handle_free_i(st_data_t key, st_data_t value, st_data_t data)
{
  ruby_xfree((char *)value);
  return ST_CONTINUE;
}

static void
inotify_handle_free(void *data)
{
  rb_inotify_handle *handle = (rb_inotify_handle *)data;

  if (handle->fd > 0) {
    close(handle->fd); // TODO: release GVL?
  }
  rb_st_foreach(handle->watchers, inotify_handle_free_i, 0);
  rb_st_free_table(handle->watchers);
  xfree(handle);
}

static size_t
inotify_handle_memsize(const void *data)
{
  return sizeof(rb_inotify_handle);
}

static const rb_data_type_t rb_inotify_handle_type = {
  .wrap_struct_name = "INotify::Watcher",
  .function = {
    .dmark = NULL,
    .dfree = inotify_handle_free,
    .dsize = inotify_handle_memsize,
  },
  .flags = RUBY_TYPED_WB_PROTECTED,
};

static inline rb_inotify_handle *
get_handle(VALUE rb_handle)
{
  rb_inotify_handle *handle;
  TypedData_Get_Struct(rb_handle, rb_inotify_handle, &rb_inotify_handle_type, handle);
  if (handle->fd <= 0) {
    rb_raise(rb_eIOError, "closed stream");
  }
  return handle;
}

static VALUE
rb_inotify_handle_allocate(VALUE klass)
{
  rb_inotify_handle *handle;
  VALUE obj = TypedData_Make_Struct(klass, rb_inotify_handle, &rb_inotify_handle_type, handle);

  handle->fd = inotify_init();
  handle->watchers = rb_st_init_numtable();

  if (handle->fd < 0) {
    rb_sys_fail("inotify_init");
  }
  return obj;
}

static VALUE
rb_inotify_handle_fileno(VALUE self)
{
  return INT2NUM(get_handle(self)->fd);
}

typedef struct {
  rb_inotify_handle *handle;
  const char * path;
  int mask;
} watch_recursive_args;

static const char *
rb_inotify_handle_watch_recursive(rb_inotify_handle *handle, const char * path, int mask)
{
  int watch_descriptor = inotify_add_watch(handle->fd, path, mask);
  if (watch_descriptor < 0) {
    return "inotify_add_watch";
  }

  fprintf(stderr, "watch_descriptor: %d\n", watch_descriptor);
  const char *path_copy = ruby_strdup(path);
  rb_st_insert(handle->watchers, watch_descriptor, (st_data_t)path_copy);

  DIR *directory = opendir(path_copy);
  if (directory == NULL) {
    return "opendir";
  }

  char child_path[PATH_MAX]; // TODO: ideally, use value from sysconf
  strcpy(child_path, path_copy);
  char *path_end = child_path + strlen(path_copy);
  *path_end = '/';
  path_end++;

  while(1)
  {
    struct dirent *entry = readdir(directory);
    if (entry == NULL) {
      break;
    }

    if ((strcmp(entry->d_name, ".\0") == 0) || (strcmp(entry->d_name, "..") == 0)) {
      continue;
    }

    if (entry->d_type != DT_DIR) {
      continue;
    }

    strcpy(path_end, entry->d_name);
    const char *error = rb_inotify_handle_watch_recursive(handle, child_path, mask);
    if (error) {
      int old_errno = errno;
      closedir(directory);
      errno = old_errno;
      return error;
    }
  }

  if (closedir(directory) < 0) {
    return "closedir";
  }

  return NULL;
}

static void *
rb_inotify_handle_watch_recursive_safe(void *_args)
{
  watch_recursive_args *args = (watch_recursive_args *)_args;
  return (void *)rb_inotify_handle_watch_recursive(args->handle, args->path, args->mask);
}

static const char *
rb_inotify_handle_watch_recursive_nogvl(rb_inotify_handle *handle, const char * path, int mask)
{
  watch_recursive_args args = {
    .handle = handle,
    .path = path,
    .mask = mask,
  };
  return (const char *)rb_thread_call_without_gvl(rb_inotify_handle_watch_recursive_safe, &args, RUBY_UBF_IO, 0);
}

static VALUE
rb_inotify_handle_watch(VALUE self, VALUE path, VALUE mask)
{
  rb_inotify_handle *handle = get_handle(self);
  FilePathValue(path);
  const char * error = rb_inotify_handle_watch_recursive_nogvl(handle, RSTRING_PTR(path), NUM2INT(mask));
  if (error) {
    rb_sys_fail(error);
  }
  return Qtrue;
}

static VALUE
rb_inotify_handle_descriptor_path(VALUE self, VALUE rb_descriptor)
{
  rb_inotify_handle *handle = get_handle(self);
  const char *path = NULL;
  if (rb_st_lookup(handle->watchers, NUM2INT(rb_descriptor), (st_data_t*)&path)) {
    VALUE str = rb_utf8_str_new_static(path, strlen(path));
    rb_obj_freeze(str);
    return str;
  }
  else {
    return Qnil;
  }
}

RUBY_FUNC_EXPORTED void
Init_rb_inotify(void)
{
  VALUE rb_mINotify = rb_const_get(rb_cObject, rb_intern("INotify"));
  VALUE rb_cNative = rb_const_get(rb_mINotify, rb_intern("Native"));
  VALUE rb_cHandle = rb_const_get(rb_cNative, rb_intern("Handle"));
  rb_define_alloc_func(rb_cHandle, rb_inotify_handle_allocate);

  rb_define_method(rb_cHandle, "fileno", rb_inotify_handle_fileno, 0);
  rb_define_method(rb_cHandle, "watch", rb_inotify_handle_watch, 2);
  rb_define_method(rb_cHandle, "descriptor_path", rb_inotify_handle_descriptor_path, 1);
}

#endif
