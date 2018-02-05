#define EFL_BETA_API_SUPPORT
#define EFL_EO_API_SUPPORT

#ifndef STAND_ALONE
#include <e.h>
#else
#include <Elementary.h>
#endif
#include <Ecore.h>
#include <Ecore_Con.h>
#include "e_mod_main.h"

#define _EET_ENTRY "config"

typedef struct _Platform Platform;

typedef struct
{
   Eina_Stringshare *id;
   Eina_Stringshare *thumbnail_url;
   Eina_Stringshare *title;
   Eina_Bool is_playable;
   Eo *download_exe;
} Playlist_Item;

typedef struct
{
   char *desc;
   char *list_id;
   char *first_id;
   char *blacklist; /* List of Eina_Stringshare */

   Platform *platform;
   Eo *start_button;
   Eina_List *items; /* List of Playlist_Item */
} Playlist;

struct _Platform
{
   char *type; /* youtube... */
   Eina_List *lists; /* List of Playlist */
   Eina_List *blacklist; /* List of Eina_Stringshare */
};

typedef struct
{
   Eina_List *platforms; /* List of Platform */
} Config;

static Config *_config = NULL;

static Eet_Data_Descriptor *_config_edd = NULL;

typedef struct
{
#ifndef STAND_ALONE
   E_Gadcon_Client *gcc;
   E_Gadcon_Popup *popup;
#endif
   Evas_Object *o_icon;
   Eo *main_box, *items_table, *no_conn_label, *error_label;
   Eina_List *items;
   char *data_buf;
   char *last_error;
   unsigned int data_buf_len;
   unsigned int data_len;

   Playlist *playing_list;
} Instance;

typedef struct
{
   Instance *inst;
   const char *name;

   Eo *start_button;
} Item_Desc;

#ifndef STAND_ALONE
static E_Module *_module = NULL;
#endif

static void
_box_update(Instance *inst, Eina_Bool clear);

static Eo *
_label_create(Eo *parent, const char *text, Eo **wref)
{
   Eo *label = wref ? *wref : NULL;
   if (!label)
     {
        label = elm_label_add(parent);
        evas_object_size_hint_align_set(label, 0.0, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(label, EVAS_HINT_EXPAND, 0.0);
        evas_object_show(label);
        if (wref) efl_wref_add(label, wref);
     }
   elm_object_text_set(label, text);
   return label;
}

static Eo *
_button_create(Eo *parent, const char *text, Eo *icon, Eo **wref, Evas_Smart_Cb cb_func, void *cb_data)
{
   Eo *bt = wref ? *wref : NULL;
   if (!bt)
     {
        bt = elm_button_add(parent);
        evas_object_size_hint_align_set(bt, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(bt, 0.0, 0.0);
        evas_object_show(bt);
        if (wref) efl_wref_add(bt, wref);
        if (cb_func) evas_object_smart_callback_add(bt, "clicked", cb_func, cb_data);
     }
   elm_object_text_set(bt, text);
   elm_object_part_content_set(bt, "icon", icon);
   return bt;
}

static Eo *
_icon_create(Eo *parent, const char *path, Eo **wref)
{
   Eo *ic = wref ? *wref : NULL;
   if (!ic)
     {
        ic = elm_icon_add(parent);
        elm_icon_standard_set(ic, path);
        evas_object_show(ic);
        if (wref) efl_wref_add(ic, wref);
     }
   return ic;
}

static void
_can_read_changed(void *data EINA_UNUSED, const Efl_Event *ev)
{
   static int max_size = 16384;
   Eina_Rw_Slice slice;
   Eo *dialer = ev->object;
   Instance *inst = efl_key_data_get(dialer, "Instance");
   if (efl_key_data_get(dialer, "can_read_changed")) return;
   efl_key_data_set(dialer, "can_read_changed", dialer);

   slice.mem = malloc(max_size);
   slice.len = max_size;

   while (efl_io_reader_can_read_get(dialer))
     {
        if (efl_io_reader_read(dialer, &slice)) goto ret;
        if (slice.len > (inst->data_buf_len - inst->data_len))
          {
             inst->data_buf_len = inst->data_len + slice.len;
             inst->data_buf = realloc(inst->data_buf, inst->data_buf_len + 1);
          }
        memcpy(inst->data_buf + inst->data_len, slice.mem, slice.len);
        inst->data_len += slice.len;
        inst->data_buf[inst->data_len] = '\0';
        slice.len = max_size;
     }
ret:
   free(slice.mem);
   efl_key_data_set(dialer, "can_read_changed", NULL);
}

static void
_dialer_delete(void *data EINA_UNUSED, const Efl_Event *ev)
{
   Eo *dialer = ev->object;
   efl_del(efl_key_data_get(dialer, "post-buffer"));
   efl_del(efl_key_data_get(dialer, "copier-buffer-dialer"));
   efl_del(dialer);
}

static Efl_Net_Dialer_Http *
_dialer_create(Eina_Bool is_get_method, const char *data, Efl_Event_Cb cb)
{
   Eo *dialer = efl_add(EFL_NET_DIALER_HTTP_CLASS, ecore_main_loop_get(),
         efl_net_dialer_http_method_set(efl_added, is_get_method?"GET":"POST"),
         efl_net_dialer_proxy_set(efl_added, NULL),
         efl_net_dialer_http_request_header_add(efl_added, "Accept-Encoding", "identity"),
         efl_event_callback_add(efl_added, EFL_IO_READER_EVENT_CAN_READ_CHANGED, _can_read_changed, NULL));
   if (cb)
      efl_event_callback_add(dialer, EFL_IO_READER_EVENT_EOS, cb, NULL);
   efl_event_callback_add(dialer, EFL_IO_READER_EVENT_EOS, _dialer_delete, NULL);

   if (!is_get_method && data)
     {
        Eina_Slice slice = { .mem = data, .len = strlen(data) };
        Eo *buffer = efl_add(EFL_IO_BUFFER_CLASS, efl_loop_get(dialer),
              efl_name_set(efl_added, "post-buffer"),
              efl_io_closer_close_on_destructor_set(efl_added, EINA_TRUE),
              efl_io_closer_close_on_exec_set(efl_added, EINA_TRUE));
        efl_io_writer_write(buffer, &slice, NULL);
        efl_key_data_set(dialer, "post-buffer", buffer);

        Eo *copier = efl_add(EFL_IO_COPIER_CLASS, efl_loop_get(dialer),
              efl_name_set(efl_added, "copier-buffer-dialer"),
              efl_io_copier_source_set(efl_added, buffer),
              efl_io_copier_destination_set(efl_added, dialer),
              efl_io_closer_close_on_destructor_set(efl_added, EINA_FALSE));
        efl_key_data_set(dialer, "copier-buffer-dialer", copier);
     }

   return dialer;
}

static Eina_Bool
_exe_output_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Exe_Event_Data *event_data = (Ecore_Exe_Event_Data *)event;
   const char *buf = event_data->data;
   Ecore_Exe *exe = event_data->exe;
   Playlist_Item *pli = ecore_exe_data_get(exe);
   if (!pli || pli->download_exe != exe) return ECORE_CALLBACK_PASS_ON;

   while (!pli->is_playable && (buf = strstr(buf, "[download] ")))
     {
        buf += 11;
        float percent = strtod(buf, NULL);
        if (percent) pli->is_playable = EINA_TRUE;
     }

   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_exe_end_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Exe_Event_Del *event_info = (Ecore_Exe_Event_Del *)event;
   Ecore_Exe *exe = event_info->exe;
   Playlist_Item *pli = ecore_exe_data_get(exe);
   if (!pli || pli->download_exe != exe) return ECORE_CALLBACK_PASS_ON;
   return ECORE_CALLBACK_DONE;
}

static void
_youtube_download(Playlist_Item *pli)
{
   char cmd[1024];
   sprintf(cmd, "youtube-dl --no-part -x \"http://youtube.com/watch?v=%s\" -o /tmp/yt-%s.opus",
         pli->id, pli->id);
   pli->download_exe = ecore_exe_pipe_run(cmd, ECORE_EXE_PIPE_READ | ECORE_EXE_PIPE_ERROR, pli);
   efl_wref_add(pli->download_exe, &(pli->download_exe));
}

static void
_playlist_html_downloaded(void *data EINA_UNUSED, const Efl_Event *event)
{
   Instance *inst = efl_key_data_get(event->object, "Instance");
   Playlist *pl = efl_key_data_get(event->object, "Playlist");
   char *id_str = strstr(inst->data_buf, "data-video-id=");
   Playlist_Item *first = NULL;
   while (id_str)
     {
        Playlist_Item *it;
        char *begin = id_str;
        char *end = strchr(id_str, '\n');
        char *end_str, *str;

        while (*begin != '\n') begin--;
        id_str += 15;
        end_str = strchr(id_str, '\"');
        it = calloc(1, sizeof(*it));
        it->id = eina_stringshare_add_length(id_str, end_str-id_str);
        if (!first) first = it;

        str = strstr(begin, "data-thumbnail-url=");
        if (str && str < end)
          {
             str += 20;
             end_str = strchr(str, '\"');
             it->thumbnail_url = eina_stringshare_add_length(str, end_str-str);
          }

        str = strstr(begin, "data-video-title=");
        if (str && str < end)
          {
             str += 18;
             end_str = strchr(str, '\"');
             it->title = eina_stringshare_add_length(str, end_str-str);
          }
        pl->items = eina_list_append(pl->items, it);
        id_str = strstr(end, "data-video-id=");
     }
   if (first) _youtube_download(first);
   _box_update(inst, EINA_TRUE);
}

static void
_playlist_start_bt_clicked(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   char request[256];
   Playlist *pl = data;
   Instance *inst = efl_key_data_get(obj, "Instance");
   if (!strcmp(pl->platform->type, "youtube"))
     {
        sprintf(request, "http://www.youtube.com/watch?v=%s&list=%s", pl->first_id, pl->list_id);
     }
   Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_TRUE, NULL, _playlist_html_downloaded);
   efl_key_data_set(dialer, "Instance", inst);
   efl_key_data_set(dialer, "Playlist", pl);
   efl_net_dialer_dial(dialer, request);
   inst->playing_list = pl;
}

static void
_box_update(Instance *inst, Eina_Bool clear)
{
   Eina_List *itr;
   Playlist *pl;

   if (!inst->main_box) return;

   if (clear)
     {
        elm_box_clear(inst->main_box);
     }

   if (!inst->playing_list)
     {
        Platform *p = eina_list_data_get(_config->platforms);
        EINA_LIST_FOREACH(p->lists, itr, pl)
          {
             Eo *b = elm_box_add(inst->main_box);
             elm_box_horizontal_set(b, EINA_TRUE);
             evas_object_size_hint_align_set(b, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(b, EVAS_HINT_EXPAND, 0.0);
             evas_object_show(b);
             elm_box_pack_end(inst->main_box, b);

             Eo *o = _label_create(b, pl->desc, NULL);
             elm_box_pack_end(b, o);

             o = _button_create(b, NULL, _icon_create(b, "media-playback-start", NULL),
                   NULL, _playlist_start_bt_clicked, pl);
             efl_key_data_set(o, "Instance", inst);
             elm_box_pack_end(b, o);
          }
     }
   else
     {
        Playlist_Item *pli;
        pl = inst->playing_list;
        EINA_LIST_FOREACH(pl->items, itr, pli)
          {
             Eo *b = elm_box_add(inst->main_box);
             elm_box_horizontal_set(b, EINA_TRUE);
             evas_object_size_hint_align_set(b, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(b, EVAS_HINT_EXPAND, 0.0);
             evas_object_show(b);
             elm_box_pack_end(inst->main_box, b);

             Eo *o = _label_create(b, pli->title, NULL);
             elm_box_pack_end(b, o);

             o = _button_create(b, NULL, _icon_create(b, "media-playback-start", NULL),
                   NULL, NULL, NULL);
             efl_key_data_set(o, "Instance", inst);
             elm_box_pack_end(b, o);
          }
     }
}

static Instance *
_instance_create()
{
   Instance *inst = calloc(1, sizeof(Instance));
   return inst;
}

static void
_config_eet_load()
{
   Eet_Data_Descriptor *platform_edd, *playlist_edd;
   if (_config_edd) return;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Playlist);
   playlist_edd = eet_data_descriptor_stream_new(&eddc);
   EET_DATA_DESCRIPTOR_ADD_BASIC(playlist_edd, Playlist, "desc", desc, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_BASIC(playlist_edd, Playlist, "list_id", list_id, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_BASIC(playlist_edd, Playlist, "first_id", first_id, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(playlist_edd, Playlist, "blacklist", blacklist);

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Platform);
   platform_edd = eet_data_descriptor_stream_new(&eddc);
   EET_DATA_DESCRIPTOR_ADD_BASIC(platform_edd, Platform, "type", type, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_LIST(platform_edd, Platform, "lists", lists, playlist_edd);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(platform_edd, Platform, "blacklist", blacklist);

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Config);
   _config_edd = eet_data_descriptor_stream_new(&eddc);
   EET_DATA_DESCRIPTOR_ADD_LIST(_config_edd, Config, "platforms", platforms, platform_edd);
}

static void
_config_save()
{
   char path[1024];
   sprintf(path, "%s/e_music/config", efreet_config_home_get());
   _config_eet_load();
   Eet_File *file = eet_open(path, EET_FILE_MODE_WRITE);
   eet_data_write(file, _config_edd, _EET_ENTRY, _config, EINA_TRUE);
   eet_close(file);
}

static Eina_Bool
_mkdir(const char *dir)
{
   if (!ecore_file_exists(dir))
     {
        Eina_Bool success = ecore_file_mkdir(dir);
        if (!success)
          {
             printf("Cannot create a config folder \"%s\"\n", dir);
             return EINA_FALSE;
          }
     }
   return EINA_TRUE;
}

static void
_config_load()
{
   Platform *p;
   Playlist *pl;
   char path[1024];

   sprintf(path, "%s/e_music", efreet_config_home_get());
   if (!_mkdir(path)) return;

   sprintf(path, "%s/e_music/config", efreet_config_home_get());
   _config_eet_load();
   Eet_File *file = eet_open(path, EET_FILE_MODE_READ);
   if (!file)
     {
        _config = calloc(1, sizeof(Config));
        /* TEMP */
        p = calloc(1, sizeof(*p));
        p->type = strdup("youtube");
        pl = calloc(1, sizeof(*pl));
        pl->platform = p;
        pl->desc = strdup("Toto");
        pl->list_id = strdup("RDEMBacXKC3mWU4Pzru44ZwIdg");
        pl->first_id = strdup("izTMmZ9WYlE");
        p->lists = eina_list_append(p->lists, pl);
        _config->platforms = eina_list_append(_config->platforms, p);
     }
   else
     {
        _config = eet_data_read(file, _config_edd, _EET_ENTRY);
        eet_close(file);
     }

   Eina_List *itr, *itr2;
   EINA_LIST_FOREACH(_config->platforms, itr, p)
     {
        EINA_LIST_FOREACH(p->lists, itr2, pl)
          {
             pl->platform = p;
          }
     }
   _config_save();
}

static void
_instance_delete(Instance *inst)
{
   if (inst->o_icon) evas_object_del(inst->o_icon);
   if (inst->main_box) evas_object_del(inst->main_box);

   free(inst);
}

static Item_Desc *
_item_find_by_name(const Eina_List *lst, const char *name)
{
   const Eina_List *itr;
   Item_Desc *d;
   EINA_LIST_FOREACH(lst, itr, d)
     {
        if (!strcmp(d->name, name)) return d;
     }
   return NULL;
}

#ifndef STAND_ALONE
static void
_popup_del(Instance *inst)
{
   E_FREE_FUNC(inst->popup, e_object_del);
}

static void
_popup_del_cb(void *obj)
{
   _popup_del(e_object_data_get(obj));
}

static void
_popup_comp_del_cb(void *data, Evas_Object *obj EINA_UNUSED)
{
   Instance *inst = data;

   E_FREE_FUNC(inst->popup, e_object_del);
}

static void
_button_cb_mouse_down(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Instance *inst;
   Evas_Event_Mouse_Down *ev;

   inst = data;
   ev = event_info;
   if (ev->button == 1)
     {
        if (!inst->popup)
          {
             Evas_Object *o;
             inst->popup = e_gadcon_popup_new(inst->gcc, 0);

             o = elm_box_add(e_comp->elm);
             evas_object_size_hint_align_set(o, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, 0.0);
             evas_object_show(o);
             efl_wref_add(o, &inst->main_box);

             _box_update(inst, EINA_FALSE);

             e_gadcon_popup_content_set(inst->popup, inst->main_box);
             e_comp_object_util_autoclose(inst->popup->comp_object,
                   _popup_comp_del_cb, NULL, inst);
             e_gadcon_popup_show(inst->popup);
             e_object_data_set(E_OBJECT(inst->popup), inst);
             E_OBJECT_DEL_SET(inst->popup, _popup_del_cb);
          }
     }
}

static E_Gadcon_Client *
_gc_init(E_Gadcon *gc, const char *name, const char *id, const char *style)
{
   Instance *inst;
   E_Gadcon_Client *gcc;
   char buf[4096];

//   printf("TRANS: In - %s\n", __FUNCTION__);

   inst = _instance_create();

   snprintf(buf, sizeof(buf), "%s/music.edj", e_module_dir_get(_module));

   inst->o_icon = edje_object_add(gc->evas);
   if (!e_theme_edje_object_set(inst->o_icon,
				"base/theme/modules/music",
                                "modules/music/main"))
      edje_object_file_set(inst->o_icon, buf, "modules/music/main");
   evas_object_show(inst->o_icon);

   gcc = e_gadcon_client_new(gc, name, id, style, inst->o_icon);
   gcc->data = inst;
   inst->gcc = gcc;

   evas_object_event_callback_add(inst->o_icon, EVAS_CALLBACK_MOUSE_DOWN,
				  _button_cb_mouse_down, inst);

   ecore_event_handler_add(ECORE_EXE_EVENT_DATA, _exe_output_cb, inst);
   ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _exe_end_cb, inst);

   return gcc;
}

static void
_gc_shutdown(E_Gadcon_Client *gcc)
{
//   printf("TRANS: In - %s\n", __FUNCTION__);
   _instance_delete(gcc->data);
}

static void
_gc_orient(E_Gadcon_Client *gcc, E_Gadcon_Orient orient EINA_UNUSED)
{
   e_gadcon_client_aspect_set(gcc, 32, 16);
   e_gadcon_client_min_size_set(gcc, 32, 16);
}

static const char *
_gc_label(const E_Gadcon_Client_Class *client_class EINA_UNUSED)
{
   return "Music";
}

static Evas_Object *
_gc_icon(const E_Gadcon_Client_Class *client_class EINA_UNUSED, Evas *evas)
{
   Evas_Object *o;
   char buf[4096];

   if (!_module) return NULL;

   snprintf(buf, sizeof(buf), "%s/e-module-music.edj", e_module_dir_get(_module));

   o = edje_object_add(evas);
   edje_object_file_set(o, buf, "icon");
   return o;
}

static const char *
_gc_id_new(const E_Gadcon_Client_Class *client_class)
{
   char buf[32];
   static int id = 0;
   sprintf(buf, "%s.%d", client_class->name, ++id);
   return eina_stringshare_add(buf);
}

EAPI E_Module_Api e_modapi =
{
   E_MODULE_API_VERSION, "Music"
};

static const E_Gadcon_Client_Class _gc_class =
{
   GADCON_CLIENT_CLASS_VERSION, "music",
   {
      _gc_init, _gc_shutdown, _gc_orient, _gc_label, _gc_icon, _gc_id_new, NULL, NULL
   },
   E_GADCON_CLIENT_STYLE_PLAIN
};

EAPI void *
e_modapi_init(E_Module *m)
{
//   printf("TRANS: In - %s\n", __FUNCTION__);
   ecore_init();
   ecore_con_init();
   ecore_con_url_init();
   efreet_init();

   _module = m;
   _config_load();
   e_gadcon_provider_register(&_gc_class);

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
//   printf("TRANS: In - %s\n", __FUNCTION__);
   e_gadcon_provider_unregister(&_gc_class);

   _module = NULL;
   efreet_shutdown();
   ecore_con_url_shutdown();
   ecore_con_shutdown();
   ecore_shutdown();
   return 1;
}

EAPI int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   //e_config_domain_save("module.music", conf_edd, cpu_conf);
   return 1;
}
#else
int main(int argc, char **argv)
{
   Instance *inst;

   eina_init();
   ecore_init();
   ecore_con_init();
   efreet_init();
   elm_init(argc, argv);

   _config_load();
   inst = _instance_create();

   Eo *win = elm_win_add(NULL, "Music", ELM_WIN_BASIC);

   Eo *o = elm_box_add(win);
   evas_object_size_hint_align_set(o, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, 0.0);
   evas_object_show(o);
   elm_win_resize_object_add(win, o);
   efl_wref_add(o, &inst->main_box);

   evas_object_resize(win, 480, 480);
   evas_object_show(win);

   ecore_event_handler_add(ECORE_EXE_EVENT_DATA, _exe_output_cb, inst);
   ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _exe_end_cb, inst);

   _box_update(inst, EINA_FALSE);

   elm_run();

   _instance_delete(inst);
   elm_shutdown();
   ecore_con_shutdown();
   ecore_shutdown();
   eina_shutdown();
   return 0;
}
#endif
