#define EFL_BETA_API_SUPPORT
#define EFL_EO_API_SUPPORT

#include <time.h>
#include <Elementary.h>
#include <Emotion.h>
#include <Ecore.h>
#include <Ecore_Con.h>

#define _EET_ENTRY "config"

typedef struct _Platform Platform;
typedef struct _Instance Instance;

typedef struct
{
   Instance *inst;

   Eina_Stringshare *id;
   Eina_Stringshare *thumbnail_path;
   Eina_Stringshare *title;

   Eina_Stringshare *download_path;
   Eo *download_exe;
   Eo *gl_item;
   Eina_Bool is_playable : 1;
   Eina_Bool playing : 1;
   Eina_Bool randomly_played : 1;
   Eina_Bool is_blocked : 1;
} Playlist_Item;

typedef struct
{
   char *desc;
   char *list_id;
   char *first_id;
   Eina_List *blacklist; /* List of char * */

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
   Eina_Bool loop_all; /* Loop on all the playlist items */
   Eina_Bool random; /* Random when choosing playlist items */
   Eina_List *platforms; /* List of Platform */
} Config;

static Config *_config = NULL;

static Eet_Data_Descriptor *_config_edd = NULL;

typedef struct
{
   char *data;
   unsigned int max_len;
   unsigned int len;
} Download_Buffer;

struct _Instance
{
   Evas_Object *o_icon;
   Eo *main, *main_box, *pl_img;
   Eo *playlist_link_entry;
   Eo *ply_emo, *play_total_lb, *play_prg_lb, *play_prg_sl;
   Eo *play_bt, *play_song_lb;
   Eo *next_bt, *prev_bt, *stop_bt;

   Ecore_Idle_Exiter *select_job;
   Ecore_Timer *timer_1s;

   Playlist *cur_playlist;
   Playlist_Item *cur_playlist_item;
   Playlist_Item *item_to_play;
};

static void
_box_update(Instance *inst);
static void
_media_next_cb(void *data, Eo *obj EINA_UNUSED, void *event_info EINA_UNUSED);

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
   EET_DATA_DESCRIPTOR_ADD_BASIC(_config_edd, Config, "loop_all", loop_all, EET_T_UINT);
   EET_DATA_DESCRIPTOR_ADD_BASIC(_config_edd, Config, "random", random, EET_T_UINT);
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
     }
   else
     {
        _config = eet_data_read(file, _config_edd, _EET_ENTRY);
        eet_close(file);
     }

   Eina_List *itr, *itr2, *itr3;
   EINA_LIST_FOREACH(_config->platforms, itr, p)
     {
        EINA_LIST_FOREACH(p->lists, itr2, pl)
          {
             char *id;
             pl->platform = p;
             EINA_LIST_FOREACH(pl->blacklist, itr3, id)
               {
                  eina_list_data_set(itr3, eina_stringshare_add(id));
//                  free(id);
               }
          }
     }
   _config_save();
}

static Eo *
_label_create(Eo *parent, const char *text, Eo **wref)
{
   Eo *label = wref ? *wref : NULL;
   if (!label)
     {
        label = elm_label_add(parent);
        evas_object_size_hint_align_set(label, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(label, 0.0, 0.0);
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

static Eo *
_check_create(Eo *parent, Eina_Bool enable, Eo **wref, Evas_Smart_Cb cb_func, void *cb_data)
{
   Eo *o = wref ? *wref : NULL;
   if (!o)
     {
        o = elm_check_add(parent);
        evas_object_size_hint_align_set(o, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(o, 0.0, 0.0);
        elm_check_selected_set(o, enable);
        evas_object_show(o);
        if (wref) efl_wref_add(o, wref);
        if (cb_func) evas_object_smart_callback_add(o, "changed", cb_func, cb_data);
     }
   return o;
}

static Eo *
_entry_create(Eo *parent, const char *text, Eo **wref)
{
   Eo *o = wref ? *wref : NULL;
   if (!o)
     {
        o = elm_entry_add(parent);
        evas_object_size_hint_align_set(o, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        evas_object_show(o);
        elm_object_text_set(o, text);
        if (wref) efl_wref_add(o, wref);
     }
   return o;
}

static void
_can_read_changed(void *data EINA_UNUSED, const Efl_Event *ev)
{
   static int max_size = 16384;
   Eina_Rw_Slice slice;
   Eo *dialer = ev->object;
   Download_Buffer *buf = efl_key_data_get(dialer, "Download_Buffer");
   if (efl_key_data_get(dialer, "can_read_changed")) return;
   efl_key_data_set(dialer, "can_read_changed", dialer);

   slice.mem = malloc(max_size);
   slice.len = max_size;

   while (efl_io_reader_can_read_get(dialer))
     {
        if (efl_io_reader_read(dialer, &slice)) goto ret;
        if (slice.len > (buf->max_len - buf->len))
          {
             buf->max_len = buf->len + slice.len;
             buf->data = realloc(buf->data, buf->max_len + 1);
          }
        memcpy(buf->data + buf->len, slice.mem, slice.len);
        buf->len += slice.len;
        buf->data[buf->len] = '\0';
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
   Download_Buffer *buf = efl_key_data_get(dialer, "Download_Buffer");
   free(buf->data);
   free(buf);
   efl_del(efl_key_data_get(dialer, "post-buffer"));
   efl_del(efl_key_data_get(dialer, "copier-buffer-dialer"));
   efl_del(dialer);
}

static Efl_Net_Dialer_Http *
_dialer_create(Eina_Bool is_get_method, const char *data, Efl_Event_Cb cb)
{
   Eo *dialer = efl_add(EFL_NET_DIALER_HTTP_CLASS, efl_main_loop_get(),
         efl_net_dialer_http_method_set(efl_added, is_get_method?"GET":"POST"),
         efl_net_dialer_proxy_set(efl_added, NULL),
         efl_net_dialer_http_request_header_add(efl_added, "Accept-Encoding", "identity"),
         efl_event_callback_add(efl_added, EFL_IO_READER_EVENT_CAN_READ_CHANGED, _can_read_changed, NULL));
   if (cb)
      efl_event_callback_priority_add(dialer, EFL_IO_READER_EVENT_EOS, EFL_CALLBACK_PRIORITY_BEFORE, cb, NULL);
   efl_event_callback_add(dialer, EFL_IO_READER_EVENT_EOS, _dialer_delete, NULL);

   if (!is_get_method && data)
     {
        Eina_Slice slice = { .mem = data, .len = strlen(data) };
        Eo *buffer = efl_add(EFL_IO_BUFFER_CLASS, efl_loop_get(dialer),
              efl_name_set(efl_added, "post-buffer"),
              efl_io_closer_close_on_invalidate_set(efl_added, EINA_TRUE),
              efl_io_closer_close_on_exec_set(efl_added, EINA_TRUE));
        efl_io_writer_write(buffer, &slice, NULL);
        efl_key_data_set(dialer, "post-buffer", buffer);

        Eo *copier = efl_add(EFL_IO_COPIER_CLASS, efl_loop_get(dialer),
              efl_name_set(efl_added, "copier-buffer-dialer"),
              efl_io_copier_source_set(efl_added, buffer),
              efl_io_copier_destination_set(efl_added, dialer),
              efl_io_closer_close_on_invalidate_set(efl_added, EINA_FALSE));
        efl_key_data_set(dialer, "copier-buffer-dialer", copier);
     }
   Download_Buffer *buf = calloc(1, sizeof(*buf));
   efl_key_data_set(dialer, "Download_Buffer", buf);

   return dialer;
}

static void
_media_play_set(Instance *inst, Playlist_Item *pli, Eina_Bool play)
{
   Playlist_Item *old_pli = inst->cur_playlist_item;
   inst->item_to_play = NULL;
   if (pli) pli->playing = play;
   if (!play || pli != old_pli)
     {
        /* Pause || the selected path is different of the played path */
        if (!play)
          {
             elm_object_part_content_set(inst->play_bt, "icon",
                   _icon_create(inst->play_bt, "media-playback-start", NULL));
          }
        if (pli != old_pli) elm_object_text_set(inst->play_song_lb, "");
        emotion_object_play_set(inst->ply_emo, EINA_FALSE);
        if (old_pli)
          {
             old_pli->playing = EINA_FALSE;
             if (old_pli->gl_item) elm_genlist_item_update(old_pli->gl_item);
          }
     }
   if (play && pli)
     {
        pli->playing = EINA_TRUE;
        elm_object_part_content_set(inst->play_bt, "icon",
              _icon_create(inst->play_bt, "media-playback-pause", NULL));
        if (pli == old_pli)
          {
             /* Play again when finished - int conversion is needed
              * because the returned values are not exactly the same. */
             if ((int)emotion_object_position_get(inst->ply_emo) == (int)emotion_object_play_length_get(inst->ply_emo))
                emotion_object_position_set(inst->ply_emo, 0);
          }
        else
          {
             inst->cur_playlist_item = pli;
             if (pli->is_playable)
               {
                  //elm_genlist_item_selected_set(pli->gl_item, EINA_FALSE);
                  elm_genlist_item_show(pli->gl_item, ELM_GENLIST_ITEM_SCROLLTO_MIDDLE);
                  elm_genlist_item_update(pli->gl_item);
                  if (pli->thumbnail_path)
                     elm_image_file_set(inst->pl_img, pli->thumbnail_path, NULL);
                  emotion_object_file_set(inst->ply_emo, pli->download_path);
               }
             elm_object_text_set(inst->play_song_lb, pli->title);
          }
        emotion_object_play_set(inst->ply_emo, EINA_TRUE);
     }
}

static void
_youtube_download(Instance *inst, Playlist_Item *pli)
{
   char cmd[1024];
   sprintf(cmd, "/tmp/yt-%s.opus", pli->id);
   pli->download_path = eina_stringshare_add(cmd);
   sprintf(cmd,
         "youtube-dl --audio-format opus --no-part -x \"http://youtube.com/watch?v=%s\" -o %s",
         pli->id, pli->download_path);
   pli->download_exe = ecore_exe_pipe_run(cmd, ECORE_EXE_PIPE_READ | ECORE_EXE_PIPE_ERROR, pli);
   efl_key_data_set(pli->download_exe, "Instance", inst);
   efl_key_data_set(pli->download_exe, "Type", "YoutubeDownload");
   efl_wref_add(pli->download_exe, &(pli->download_exe));
}

static void
_pli_download(Instance *inst, Playlist_Item *pli, Eina_Bool play)
{
   int i = 0;
   Eina_List *itr = inst->cur_playlist->items;
   if (play)
     {
        if (pli->is_playable) _media_play_set(inst, pli, EINA_TRUE);
        else inst->item_to_play = pli;
     }
   if (!pli->is_playable && !pli->download_exe && !pli->is_blocked)
     {
        _youtube_download(inst, pli);
     }
   if (play)
     {
         while (itr && eina_list_data_get(itr) != pli)
           {
              itr = eina_list_next(itr);
           }
         for (i = 0; i < 10; i++)
           {
              itr = eina_list_next(itr);
              if (!itr) continue;
              _pli_download(inst, eina_list_data_get(itr), EINA_FALSE);
           }
     }
}

static void
_pli_icon_downloaded(void *data EINA_UNUSED, const Efl_Event *event)
{
   Download_Buffer *buf = efl_key_data_get(event->object, "Download_Buffer");
   Playlist_Item *pli = efl_key_data_get(event->object, "pli");
   if (!buf->data) return;
   char fpath[256];
   sprintf(fpath, "/tmp/yt-%s.png", pli->id);
   FILE *fp = fopen(fpath, "w");
   fwrite(buf->data, 1, buf->len, fp);
   fclose(fp);
   pli->thumbnail_path = eina_stringshare_add(fpath);
   if (pli->gl_item) elm_genlist_item_update(pli->gl_item);
}

static void
_playlist_html_downloaded(void *data EINA_UNUSED, const Efl_Event *event)
{
   char cmd[100];
   Instance *inst = efl_key_data_get(event->object, "Instance");
   Playlist *pl = efl_key_data_get(event->object, "Playlist");
   Download_Buffer *buf = efl_key_data_get(event->object, "Download_Buffer");
   char *id_str = strstr(buf->data, "data-video-id=");
   while (id_str)
     {
        Eina_Stringshare *blackid;
        Eina_List *itr;
        Playlist_Item *it;
        char *begin = id_str;
        char *end = strchr(id_str, '\n');
        char *end_str, *str;

        while (*begin != '\n') begin--;
        id_str += 15;
        end_str = strchr(id_str, '\"');

        it = calloc(1, sizeof(*it));
        it->inst = inst;
        it->id = eina_stringshare_add_length(id_str, end_str-id_str);

        EINA_LIST_FOREACH(pl->blacklist, itr, blackid)
           if (blackid == it->id) it->is_blocked = EINA_TRUE;
        if (!it->is_blocked)
           EINA_LIST_FOREACH(pl->platform->blacklist, itr, blackid)
              if (blackid == it->id) it->is_blocked = EINA_TRUE;
        str = strstr(begin, "data-thumbnail-url=");
        if (str && str < end)
          {
             char url[1024];
             str += 20;
             end_str = strchr(str, '\"');
             memcpy(url, str, end_str-str);
             url[end_str-str] = '\0';
             Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_TRUE, NULL, _pli_icon_downloaded);
             efl_key_data_set(dialer, "pli", it);
             efl_net_dialer_dial(dialer, url);
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

   if (!pl->desc)
     {
        char *desc_str = strstr(buf->data, "data-list-title=");
        if (desc_str)
          {
             desc_str += 17;
             char *end_str = strchr(desc_str, '\"');
             pl->desc = strndup(desc_str, end_str - desc_str);
             _config_save();
          }
     }

   sprintf(cmd, "youtube-dl -j --flat-playlist \"%s\"", pl->list_id);
   Ecore_Exe *exe = ecore_exe_pipe_run(cmd, ECORE_EXE_PIPE_READ | ECORE_EXE_PIPE_ERROR, pl);
   efl_key_data_set(exe, "Instance", inst);
   efl_key_data_set(exe, "Type", "YoutubeGetFullPlaylist");
   efl_key_data_set(exe, "Download_Buffer", calloc(1, sizeof(Download_Buffer)));

   _media_next_cb(inst, NULL, NULL);
   _box_update(inst);
}

static int
_ascii_to_digit(char c)
{
   if (c >= '0' && c <= '9') return c - '0';
   if (c >= 'a' && c <= 'f') return c - 'a' + 0xA;
   if (c >= 'A' && c <= 'F') return c - 'A' + 0xA;
   return 0;
}

static void
_playlist_item_json_downloaded(void *data EINA_UNUSED, const Efl_Event *event)
{
   Playlist_Item *pli = efl_key_data_get(event->object, "Playlist_Item");
   Download_Buffer *buf = efl_key_data_get(event->object, "Download_Buffer");
   if (!buf->data) return;
   char *str = strstr(buf->data, "thumbnail_url\":\""), *end_str;
   if (str)
     {
        Eina_Strbuf *sbuf = eina_strbuf_new();
        str += 16;
        end_str = strchr(str, '\"');
        *end_str = '\0';
        eina_strbuf_append(sbuf, str);
        *end_str = '\"';
        eina_strbuf_replace_all(sbuf, "\\/", "/");
        Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_TRUE, NULL, _pli_icon_downloaded);
        efl_key_data_set(dialer, "pli", pli);
        efl_net_dialer_dial(dialer, eina_strbuf_string_get(sbuf));
        eina_strbuf_free(sbuf);
     }
   str = strstr(buf->data, "title\":\"");
   if (str)
     {
        str += 8;
        end_str = strchr(str, '\"');
        *end_str = '\0';
        if (strstr(str, "\\u"))
          {
             int i = 0;
             char title[4096];
             while (*str)
               {
                  if (*str == '\\' && *(str + 1) == 'u')
                    {
                       int len = 0;
                       str += 2;
                       Eina_Unicode uni[2];
                       uni[0] = (_ascii_to_digit(str[0]) << 12) | (_ascii_to_digit(str[1]) << 8) |
                             (_ascii_to_digit(str[2]) << 4) | _ascii_to_digit(str[3]);
                       uni[1] = 0;
                       char *utf = eina_unicode_unicode_to_utf8(uni, &len);
                       memcpy(title + i, utf, len);
                       str += 4;
                       i += len;
                    }
                  else
                    {
                       title[i++] = *str;
                       str++;
                    }
               }
             title[i] = '\0';
             pli->title = eina_stringshare_add(title);
          }
        else
           pli->title = eina_stringshare_add(str);
        *end_str = '\"';
     }
   if (pli->gl_item) elm_genlist_item_update(pli->gl_item);
}

static void
_playlist_start_bt_clicked(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   char request[256];
   Playlist *pl = data;
   Instance *inst = efl_key_data_get(obj, "Instance");
   if (pl->platform->type == NULL) return;
   if (!strcmp(pl->platform->type, "youtube"))
     {
        sprintf(request, "http://www.youtube.com/watch?v=%s&list=%s", pl->first_id, pl->list_id);
     }
   Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_TRUE, NULL, _playlist_html_downloaded);
   efl_key_data_set(dialer, "Instance", inst);
   efl_key_data_set(dialer, "Playlist", pl);
   efl_net_dialer_dial(dialer, request);
   inst->cur_playlist = pl;
}

static Eina_Bool
_exe_error_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   //Ecore_Exe_Event_Data *event_data = (Ecore_Exe_Event_Data *)event;
   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_exe_output_cb(void *data EINA_UNUSED, int _type EINA_UNUSED, void *event)
{
   Ecore_Exe_Event_Data *event_data = (Ecore_Exe_Event_Data *)event;
   const char *buf = event_data->data;
   unsigned int size = event_data->size;
   Ecore_Exe *exe = event_data->exe;
   Instance *inst = efl_key_data_get(exe, "Instance");
   const char *type = efl_key_data_get(exe, "Type");
   if (!strcmp(type, "YoutubeDownload"))
     {
        Playlist_Item *pli = ecore_exe_data_get(exe);
        while (!pli->is_playable && (buf = strstr(buf, "[download] ")))
          {
             buf += 11;
             float percent = strtod(buf, NULL);
             if (percent)
               {
                  pli->is_playable = EINA_TRUE;
                  if (pli == inst->item_to_play) _media_play_set(inst, pli, EINA_TRUE);
               }
          }
     }
   else if (!strcmp(type, "YoutubeGetFullPlaylist"))
     {
        Download_Buffer *dbuf = efl_key_data_get(exe, "Download_Buffer");
        if (size > (dbuf->max_len - dbuf->len))
          {
             dbuf->max_len = dbuf->len + size;
             dbuf->data = realloc(dbuf->data, dbuf->max_len + 1);
             memcpy(dbuf->data+dbuf->len, buf, size);
             dbuf->len += size;
             dbuf->data[dbuf->max_len] = '\0';
          }
     }

   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_exe_end_cb(void *data EINA_UNUSED, int _type EINA_UNUSED, void *event)
{
   Ecore_Exe_Event_Del *event_info = (Ecore_Exe_Event_Del *)event;
   Ecore_Exe *exe = event_info->exe;
   Instance *inst = efl_key_data_get(exe, "Instance");
   const char *type = efl_key_data_get(exe, "Type");
   if (!type) return ECORE_CALLBACK_PASS_ON;
   if (event_info->exit_code) return ECORE_CALLBACK_DONE;
   if (!strcmp(type, "YoutubeDownload"))
     {
        Playlist_Item *pli = ecore_exe_data_get(exe);
        pli->is_playable = EINA_TRUE;
        if (pli == inst->item_to_play) _media_play_set(inst, pli, EINA_TRUE);
     }
   else if (!strcmp(type, "YoutubeGetFullPlaylist"))
     {
        Playlist *pl = ecore_exe_data_get(exe);
        Download_Buffer *dbuf = efl_key_data_get(exe, "Download_Buffer");
        char *id_str = strstr(dbuf->data, "\"id\": \"");
        Eina_List *old_items = pl->items;
        pl->items = NULL;
        while (id_str)
          {
             Eina_List *itr;
             Playlist_Item *pli, *found_old = NULL;
             id_str += 7;
             char *end_str = strchr(id_str, '\"');
             Eina_Stringshare *id = eina_stringshare_add_length(id_str, end_str-id_str);
             EINA_LIST_FOREACH(old_items, itr, pli)
               {
                  if (!found_old && pli->id == id) found_old = pli;
               }
             if (found_old)
               {
                  pli = found_old;
                  elm_object_item_del(pli->gl_item);
               }
             else
               {
                  char request[256];
                  pli = calloc(1, sizeof(*pli));
                  pli->id = id;
                  pli->inst = inst;
                  sprintf(request,
                        "https://www.youtube.com/oembed?url=https://www.youtube.com/watch?v=%s&format=json",
                        id);
                  Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_TRUE, NULL, _playlist_item_json_downloaded);
                  efl_key_data_set(dialer, "Instance", inst);
                  efl_key_data_set(dialer, "Playlist_Item", pli);
                  efl_net_dialer_dial(dialer, request);
               }
             pl->items = eina_list_append(pl->items, pli);
             id_str = strstr(id_str, "\"id\": \"");
          }
        _box_update(inst);
     }
   return ECORE_CALLBACK_DONE;
}

static Playlist_Item *
_random_item_choose(Instance *inst)
{
   Eina_List *itr;
   Playlist_Item *pli;
   int v;
   int max = 0, nb_unblocked = 0;
   EINA_LIST_FOREACH(inst->cur_playlist->items, itr, pli)
     {
        if (!pli->is_blocked)
          {
             nb_unblocked++;
             if (!pli->randomly_played) max++;
          }
     }
   if (!max && _config->loop_all)
     {
        EINA_LIST_FOREACH(inst->cur_playlist->items, itr, pli)
          {
             pli->randomly_played = EINA_FALSE;
          }
        max = nb_unblocked;
     }
   if (max)
     {
        v = rand() % max;
        EINA_LIST_FOREACH(inst->cur_playlist->items, itr, pli)
          {
             if (!pli->is_blocked && !pli->randomly_played)
               {
                  if (!v)
                    {
                       pli->randomly_played = EINA_TRUE;
                       return pli;
                    }
                  v--;
               }
          }
     }
   return NULL;
}

static void
_media_play_pause_cb(void *data, Eo *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Instance *inst = data;
   if (!inst->cur_playlist_item)
      inst->cur_playlist_item = eina_list_data_get(inst->cur_playlist->items);
   _media_play_set(inst, inst->cur_playlist_item, !inst->cur_playlist_item->playing);
}

static void
_media_stop_cb(void *data, Eo *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Instance *inst = data;
   _media_play_set(inst, NULL, EINA_FALSE);
   efl_del(inst->ply_emo);
   inst->cur_playlist = NULL;
   _box_update(inst);
}

static Playlist_Item *
_next_item_find(Instance *inst, Playlist_Item *cur_pli)
{
   if (_config->random) return _random_item_choose(inst);
   if (!cur_pli) cur_pli = inst->cur_playlist_item;
   Eina_List *itr = eina_list_data_find_list(inst->cur_playlist->items, cur_pli);
   int nb_items = eina_list_count(inst->cur_playlist->items), i = 0;
   Playlist_Item *next_pli;
   do
     {
        if (!itr && (!i || _config->loop_all)) itr = inst->cur_playlist->items;
        else itr = eina_list_next(itr);
        if (!itr && _config->loop_all) itr = inst->cur_playlist->items;
        next_pli = eina_list_data_get(itr);
        i++;
     } while (next_pli->is_blocked && i < nb_items);
   if (!next_pli->is_blocked) return next_pli;
   else return NULL;
}

static Playlist_Item *
_prev_item_find(Instance *inst)
{
   if (_config->random) return _random_item_choose(inst);
   Eina_List *itr = eina_list_data_find_list(inst->cur_playlist->items, inst->cur_playlist_item);
   int nb_items = eina_list_count(inst->cur_playlist->items), i = 0;
   Playlist_Item *prev_pli;
   do
     {
        if (!itr && (!i || _config->loop_all)) itr = inst->cur_playlist->items;
        else itr = eina_list_prev(itr);
        if (!itr && _config->loop_all) itr = eina_list_last(inst->cur_playlist->items);
        prev_pli = eina_list_data_get(itr);
        i++;
     } while (prev_pli->is_blocked && i < nb_items);
   if (!prev_pli->is_blocked) return prev_pli;
   else return NULL;
}

static void
_media_next_cb(void *data, Eo *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Instance *inst = data;
   Playlist_Item *next_pli = _next_item_find(inst, NULL);
   if (next_pli) _pli_download(inst, next_pli, EINA_TRUE);
}

static void
_media_prev_cb(void *data, Eo *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Instance *inst = data;
   Playlist_Item *prev_pli = _prev_item_find(inst);
   if (prev_pli) _pli_download(inst, prev_pli, EINA_TRUE);
}

static void
_media_length_update(void *data, const Efl_Event *ev EINA_UNUSED)
{
   Instance *inst = data;
   double val = emotion_object_play_length_get(inst->ply_emo);
   char str[16];
   sprintf(str, "%.2d:%.2d:%.2d", ((int)val) / 3600, (((int)val) % 3600) / 60, ((int)val) % 60);
   if (inst->play_total_lb) elm_object_text_set(inst->play_total_lb, str);
   if (inst->play_prg_sl) elm_slider_min_max_set(inst->play_prg_sl, 0, val);
}

static void
_media_position_update(void *data, const Efl_Event *ev EINA_UNUSED)
{
   Instance *inst = data;
   double val = emotion_object_position_get(inst->ply_emo);
   char str[16];
   sprintf(str, "%.2d:%.2d:%.2d", ((int)val) / 3600, (((int)val) % 3600) / 60, ((int)val) % 60);
   if (inst->play_prg_lb) elm_object_text_set(inst->play_prg_lb, str);
   if (inst->play_prg_sl) elm_slider_value_set(inst->play_prg_sl, val);
}

static void
_media_finished(void *data, const Efl_Event *ev EINA_UNUSED)
{
   Instance *inst = data;
   _media_play_set(inst, inst->cur_playlist_item, EINA_FALSE);
   _media_next_cb(inst, NULL, NULL);
}

static void
_loop_state_changed(void *data EINA_UNUSED, Eo *chk, void *event_info EINA_UNUSED)
{
   _config->loop_all = elm_check_selected_get(chk);
   _config_save();
}

static void
_random_state_changed(void *data EINA_UNUSED, Eo *chk, void *event_info EINA_UNUSED)
{
   Instance *inst = data;
   Eina_List *itr;
   Playlist_Item *pli;
   _config->random = elm_check_selected_get(chk);
   _config_save();
   if (!inst->cur_playlist) return;
   EINA_LIST_FOREACH(inst->cur_playlist->items, itr, pli)
     {
        pli->randomly_played = EINA_FALSE;
     }
}

static char *
_sl_format(double val)
{
   char str[100];
   sprintf(str, "%.2d:%.2d:%.2d", ((int)val) / 3600, (((int)val) % 3600) / 60, ((int)val) % 60);
   return strdup(str);
}

static void
_sl_label_free(char *str)
{
   free(str);
}

static void
_sl_changed(void *data, const Efl_Event *ev EINA_UNUSED)
{
   Instance *inst = data;
   double val = elm_slider_value_get(inst->play_prg_sl);
   emotion_object_position_set(inst->ply_emo, val);
}

static char *
_pl_item_text_get(void *data, Evas_Object *obj EINA_UNUSED, const char *part EINA_UNUSED)
{
   char buffer[4096];
   Playlist_Item *pli = data;
   if (!pli->title)
      sprintf(buffer, "Unknown");
   else if (pli->is_blocked)
      sprintf(buffer, "<i><color=#888>%s</color></i>", pli->title);
   else if (pli->playing)
      sprintf(buffer, "<b><color=#0FF>%s</color></b>", pli->title);
   else
      sprintf(buffer, pli->title);
   return strdup(buffer);
}

static void
_pl_item_ban_from_playlist(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Playlist_Item *pli = data;
   Instance *inst = pli->inst;
   pli->is_blocked = EINA_TRUE;
   inst->cur_playlist->blacklist = eina_list_append(inst->cur_playlist->blacklist, pli->id);
   _config_save();
   if (pli->gl_item) elm_genlist_item_update(pli->gl_item);
}

static void
_pl_item_ban_from_all_playlists(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Playlist_Item *pli = data;
   Instance *inst = pli->inst;
   pli->is_blocked = EINA_TRUE;
   inst->cur_playlist->platform->blacklist = eina_list_append(inst->cur_playlist->platform->blacklist, pli->id);
   _config_save();
   if (pli->gl_item) elm_genlist_item_update(pli->gl_item);
}

static void
_pl_item_unban_from_all_playlists(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Playlist_Item *pli = data;
   Instance *inst = pli->inst;
   pli->is_blocked = EINA_FALSE;
   inst->cur_playlist->blacklist = eina_list_remove(inst->cur_playlist->blacklist, pli->id);
   inst->cur_playlist->platform->blacklist = eina_list_remove(inst->cur_playlist->platform->blacklist, pli->id);
   _config_save();
   if (pli->gl_item) elm_genlist_item_update(pli->gl_item);
}

static void
_pl_item_options_show(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Playlist_Item *pli = data;
   if (pli->inst->select_job) ecore_idle_enterer_del(pli->inst->select_job);
   pli->inst->select_job = NULL;

   Eo *hv = elm_hover_add(pli->inst->main_box);
   elm_hover_parent_set(hv, pli->inst->main_box);
   elm_hover_target_set(hv, obj);
   efl_gfx_entity_visible_set(hv, EINA_TRUE);
   Eo *bx = elm_box_add(hv);
   elm_box_homogeneous_set(bx, EINA_TRUE);

   Eo *bt = _button_create(bx, "Ban from this playlist", NULL, NULL, _pl_item_ban_from_playlist, pli);
   elm_object_disabled_set(bt, pli->is_blocked);
   elm_box_pack_end(bx, bt);

   bt = _button_create(bx, "Ban from all the playlists", NULL, NULL, _pl_item_ban_from_all_playlists, pli);
   elm_object_disabled_set(bt, pli->is_blocked);
   elm_box_pack_end(bx, bt);

   bt = _button_create(bx, "Unban", NULL, NULL, _pl_item_unban_from_all_playlists, pli);
   elm_object_disabled_set(bt, !pli->is_blocked);
   elm_box_pack_end(bx, bt);

   evas_object_show(bx);
   elm_object_part_content_set(hv, "top", bx);
}

static Evas_Object *
_pl_item_content_get(void *data, Evas_Object *gl, const char *part)
{
   Playlist_Item *pli = data;
   Eo *ret = NULL;
   if (!strcmp(part, "elm.swallow.icon"))
     {
        ret = elm_icon_add(gl);
        if (pli->thumbnail_path) elm_image_file_set(ret, pli->thumbnail_path, NULL);
        evas_object_size_hint_aspect_set(ret, EVAS_ASPECT_CONTROL_VERTICAL, 1, 1);
     }
   else if (!strcmp(part, "elm.swallow.end"))
     {
        ret = _button_create(pli->inst->main, "",
              _icon_create(gl, "view-list-details", NULL),
              NULL, _pl_item_options_show, pli);
     }
   return ret;
}

static Eina_Bool
_playlist_item_select(void *data)
{
   Playlist_Item *pli = data;
   if (pli->inst->select_job && !pli->is_blocked) _pli_download(pli->inst, pli, EINA_TRUE);
   pli->inst->select_job = NULL;
   return ECORE_CALLBACK_CANCEL;
}

static void
_playlist_item_selected(void *data EINA_UNUSED, Evas_Object *gl EINA_UNUSED, void *event_info)
{
   Playlist_Item *pli = elm_object_item_data_get(event_info);
   if (pli->inst->timer_1s)
     {
        elm_genlist_item_selected_set(event_info, EINA_FALSE);
        return;
     }
   if (pli->inst->select_job) ecore_idle_enterer_del(pli->inst->select_job);
   pli->inst->select_job = ecore_idle_enterer_add(_playlist_item_select, pli);
}

static Eina_Bool
_playlist_link_selection_get(void *data EINA_UNUSED,
                   Evas_Object *en,
                   Elm_Selection_Data *sel_data)
{
   if (!sel_data->len) return EINA_FALSE;
   char *buf = alloca(sel_data->len + 256);
   sprintf(buf, "<i><color=#888>%s</color></i>", (char *)sel_data->data);
   elm_object_text_set(en, buf);
   buf = efl_key_data_get(en, "pasted_data");
   if (buf) free(buf);
   efl_key_data_set(en, "pasted_data", strdup(sel_data->data));
   return EINA_TRUE;
}

static Eina_Bool
_playlist_link_selection_poll_cb(void *data)
{
   Instance *inst = data;
   if (!inst->playlist_link_entry) return EINA_FALSE;
   elm_cnp_selection_get
      (inst->playlist_link_entry, ELM_SEL_TYPE_CLIPBOARD, ELM_SEL_FORMAT_TEXT,
       _playlist_link_selection_get, NULL);
   return EINA_TRUE;
}

static void
_playlist_link_add(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Instance *inst = data;
   char *text = efl_key_data_get(inst->playlist_link_entry, "pasted_data");
   if (strstr(text, "youtube"))
     {
        Eina_List *itr;
        Platform *p;
        Eina_Bool p_found = EINA_FALSE;
        char *first_id = strstr(text, "v="), *end;
        if (!first_id) return;
        first_id += 2;
        char *list_id = strstr(text, "list=");
        if (!list_id) return;
        list_id += 5;

        Playlist *pl = calloc(1, sizeof(*pl));
        end = first_id;
        while (*end && *end != '&') end++;
        pl->first_id = strndup(first_id, end - first_id);

        end = list_id;
        while (*end && *end != '&') end++;
        pl->list_id = strndup(list_id, end - list_id);

        EINA_LIST_FOREACH(_config->platforms, itr, p)
          {
             if (!p_found && !strcmp(p->type, "youtube"))
               {
                  p->lists = eina_list_append(p->lists, pl);
                  pl->platform = p;
                  p_found = EINA_TRUE;
               }
          }
        if (!p_found)
          {
             p = calloc(1, sizeof(*p));
             p->type = "youtube";
             p->lists = eina_list_append(p->lists, pl);
             pl->platform = p;
             _config->platforms = eina_list_append(_config->platforms, p);
          }
        _config_save();
        _box_update(inst);
     }
   printf("Text %s\n", text);
}

static void
_box_update(Instance *inst)
{
   static Playlist *cur_pl = NULL;
   Eina_List *itr;
   Playlist *pl;

   if (!inst->main_box) return;

   if (!cur_pl || cur_pl != inst->cur_playlist)
     {
        elm_box_clear(inst->main_box);
     }
   cur_pl = inst->cur_playlist;

   if (!cur_pl)
     {
        Eo *o;
        int row = 0;
        Platform *p = eina_list_data_get(_config->platforms);

        Eo *tb = elm_table_add(inst->main_box);
        evas_object_size_hint_align_set(tb, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(tb, EVAS_HINT_EXPAND, 0.0);
        evas_object_show(tb);
        elm_box_pack_end(inst->main_box, tb);
        EINA_LIST_FOREACH(p ? p->lists : NULL, itr, pl)
          {
             o = _label_create(tb, pl->desc ? pl->desc : "Still unknown", NULL);
             elm_table_pack(tb, o, 0, row, 1, 1);

             o = _button_create(tb, NULL, _icon_create(tb, "media-playback-start", NULL),
                   NULL, _playlist_start_bt_clicked, pl);
             efl_key_data_set(o, "Instance", inst);
             elm_table_pack(tb, o, 1, row, 1, 1);
             row++;
          }
        o = inst->playlist_link_entry = _entry_create(tb, "Copy a playlist link so it is pasted here",
              &(inst->playlist_link_entry));
        ecore_timer_add(5.0, _playlist_link_selection_poll_cb, inst);
        elm_cnp_selection_get
           (o, ELM_SEL_TYPE_CLIPBOARD, ELM_SEL_FORMAT_TEXT, _playlist_link_selection_get, NULL);
        elm_entry_single_line_set(o, EINA_TRUE);
        elm_table_pack(tb, o, 0, row, 1, 1);
        o = _button_create(tb, NULL, _icon_create(tb, "list-add", NULL),
                 NULL, _playlist_link_add, inst);
        elm_table_pack(tb, o, 1, row, 1, 1);
     }
   else
     {
        static Elm_Genlist_Item_Class *_pl_itc = NULL;
        static Eo *playlist_box = NULL, *playlist_gl = NULL;
        static Eo *ply_box = NULL, *ply_sl_box = NULL, *ply_bts_box = NULL;
        Playlist_Item *pli;
        Eina_Bool bts_create = EINA_FALSE;

        if (!playlist_box)
          {
             playlist_box = elm_box_add(inst->main_box);
             elm_box_horizontal_set(playlist_box, EINA_TRUE);
             evas_object_size_hint_weight_set(playlist_box, EVAS_HINT_EXPAND, 0.9);
             evas_object_size_hint_align_set(playlist_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
             efl_gfx_hint_size_min_set(inst->main_box, EINA_SIZE2D(800, 400));
             elm_box_pack_end(inst->main_box, playlist_box);
             evas_object_show(playlist_box);
             efl_wref_add(playlist_box, &playlist_box);
          }

        if (!playlist_gl)
          {
             playlist_gl = elm_genlist_add(playlist_box);
             evas_object_size_hint_align_set(playlist_gl, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(playlist_gl, 0.6, EVAS_HINT_EXPAND);
             elm_box_pack_end(playlist_box, playlist_gl);
             evas_object_show(playlist_gl);
             evas_object_smart_callback_add(playlist_gl, "selected", _playlist_item_selected, NULL);
             efl_wref_add(playlist_gl, &playlist_gl);
          }

        if (!_pl_itc)
          {
             _pl_itc = elm_genlist_item_class_new();
             _pl_itc->item_style = "default_style";
             _pl_itc->func.text_get = _pl_item_text_get;
             _pl_itc->func.content_get = _pl_item_content_get;
          }

        EINA_LIST_FOREACH(inst->cur_playlist->items, itr, pli)
          {
             if (pli->gl_item) continue;
             pli->gl_item = elm_genlist_item_append(playlist_gl, _pl_itc, pli,
                   NULL, ELM_GENLIST_ITEM_NONE, NULL, NULL);
             efl_wref_add(pli->gl_item, &(pli->gl_item));
             if (pli->playing) elm_genlist_item_show(pli->gl_item, ELM_GENLIST_ITEM_SCROLLTO_TOP);
          }

        if (!inst->pl_img)
          {
             inst->pl_img = elm_image_add(playlist_box);
             evas_object_size_hint_weight_set(inst->pl_img, 0.4, EVAS_HINT_EXPAND);
             evas_object_size_hint_align_set(inst->pl_img, EVAS_HINT_FILL, EVAS_HINT_FILL);
             elm_box_pack_end(playlist_box, inst->pl_img);
             efl_wref_add(inst->pl_img, &inst->pl_img);
             evas_object_show(inst->pl_img);
          }

        if (!inst->ply_emo)
          {
             inst->ply_emo = emotion_object_add(evas_object_evas_get(inst->main));
             efl_weak_ref(&inst->ply_emo);
             emotion_object_init(inst->ply_emo, NULL);
             efl_event_callback_add
                (inst->ply_emo, EFL_CANVAS_VIDEO_EVENT_LENGTH_CHANGE, _media_length_update, inst);
             efl_event_callback_add
                (inst->ply_emo, EFL_CANVAS_VIDEO_EVENT_POSITION_CHANGE, _media_position_update, inst);
             efl_event_callback_add
                (inst->ply_emo, EFL_CANVAS_VIDEO_EVENT_PLAYBACK_STOP, _media_finished, inst);
          }

        /* Player vertical box */
        if (!ply_box)
          {
             ply_box = elm_box_add(inst->main_box);
             evas_object_size_hint_weight_set(ply_box, EVAS_HINT_EXPAND, 0.1);
             evas_object_size_hint_align_set(ply_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
             elm_box_pack_end(inst->main_box, ply_box);
             evas_object_show(ply_box);
             efl_wref_add(ply_box, &ply_box);
          }

        /* Player slider horizontal box */
        if (!ply_sl_box)
          {
             ply_sl_box = elm_box_add(ply_box);
             evas_object_size_hint_weight_set(ply_sl_box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
             evas_object_size_hint_align_set(ply_sl_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
             elm_box_horizontal_set(ply_sl_box, EINA_TRUE);
             elm_box_pack_end(ply_box, ply_sl_box);
             evas_object_show(ply_sl_box);
             efl_wref_add(ply_sl_box, &ply_sl_box);
          }

        /* Label showing music progress */
        if (!inst->play_prg_lb)
          {
             inst->play_prg_lb = elm_label_add(ply_sl_box);
             evas_object_size_hint_align_set(inst->play_prg_lb, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(inst->play_prg_lb, 0.1, EVAS_HINT_EXPAND);
             elm_box_pack_end(ply_sl_box, inst->play_prg_lb);
             efl_weak_ref(&inst->play_prg_lb);
             evas_object_show(inst->play_prg_lb);
          }

        /* Slider showing music progress */
        if (!inst->play_prg_sl)
          {
             inst->play_prg_sl = elm_slider_add(ply_sl_box);
             elm_slider_indicator_format_function_set(inst->play_prg_sl, _sl_format, _sl_label_free);
             elm_slider_span_size_set(inst->play_prg_sl, 120);
             efl_event_callback_add(inst->play_prg_sl, EFL_UI_RANGE_EVENT_CHANGED, _sl_changed, inst);
             evas_object_size_hint_align_set(inst->play_prg_sl, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(inst->play_prg_sl, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
             elm_box_pack_end(ply_sl_box, inst->play_prg_sl);
             efl_weak_ref(&inst->play_prg_sl);
             evas_object_show(inst->play_prg_sl);
          }

        /* Label showing total music time */
        if (!inst->play_total_lb)
          {
             inst->play_total_lb = elm_label_add(ply_sl_box);
             evas_object_size_hint_align_set(inst->play_total_lb, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(inst->play_total_lb, 0.1, EVAS_HINT_EXPAND);
             elm_box_pack_end(ply_sl_box, inst->play_total_lb);
             efl_weak_ref(&inst->play_total_lb);
             evas_object_show(inst->play_total_lb);
          }

        /* Player song name */
        if (!inst->play_song_lb)
          {
             inst->play_song_lb = elm_label_add(ply_box);
             evas_object_size_hint_align_set(inst->play_song_lb, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(inst->play_song_lb, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
             elm_box_pack_end(ply_box, inst->play_song_lb);
             efl_weak_ref(&inst->play_song_lb);
             evas_object_show(inst->play_song_lb);
          }

        /* Player buttons box */
        if (!ply_bts_box)
          {
             bts_create = EINA_TRUE;
             ply_bts_box = elm_box_add(ply_box);
             evas_object_size_hint_weight_set(ply_bts_box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
             evas_object_size_hint_align_set(ply_bts_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
             elm_box_horizontal_set(ply_bts_box, EINA_TRUE);
             elm_box_pack_end(ply_box, ply_bts_box);
             evas_object_show(ply_bts_box);
             efl_wref_add(ply_bts_box, &ply_bts_box);
          }

        /* Prev button */
        if (!inst->prev_bt)
          {
             inst->prev_bt = _button_create(ply_bts_box, NULL,
                   _icon_create(ply_bts_box,
                      "media-seek-backward", NULL),
                   NULL, _media_prev_cb, inst);
             elm_box_pack_end(ply_bts_box, inst->prev_bt);
             efl_weak_ref(&inst->prev_bt);
          }

        /* Play/pause button */
        if (!inst->play_bt)
          {
             inst->play_bt = _button_create(ply_bts_box, NULL,
                   _icon_create(ply_bts_box,
                      inst->cur_playlist_item && inst->cur_playlist_item->playing ?
                      "media-playback-pause" : "media-playback-start", NULL),
                   NULL, _media_play_pause_cb, inst);
             elm_box_pack_end(ply_bts_box, inst->play_bt);
             efl_weak_ref(&inst->play_bt);
          }

        /* Stop button */
        if (!inst->stop_bt)
          {
             inst->stop_bt = _button_create(ply_bts_box, NULL,
                   _icon_create(ply_bts_box, "media-playback-stop", NULL),
                   NULL, _media_stop_cb, inst);
             elm_box_pack_end(ply_bts_box, inst->stop_bt);
             efl_weak_ref(&inst->stop_bt);
          }

        /* Next button */
        if (!inst->next_bt)
          {
             inst->next_bt = _button_create(ply_bts_box, NULL,
                   _icon_create(ply_bts_box,
                      "media-seek-forward", NULL),
                   NULL, _media_next_cb, inst);
             elm_box_pack_end(ply_bts_box, inst->next_bt);
             efl_weak_ref(&inst->next_bt);
          }

        if (bts_create)
          {
             /* Loop label */
             Eo *o = _label_create(ply_bts_box, "   Loop ", NULL);
             elm_box_pack_end(ply_bts_box, o);
             /* Loop checkbox */
             o = _check_create(ply_bts_box, _config->loop_all, NULL, _loop_state_changed, NULL);
             elm_box_pack_end(ply_bts_box, o);

             /* Random label */
             o = _label_create(ply_bts_box, "   Random ", NULL);
             elm_box_pack_end(ply_bts_box, o);
             /* Random checkbox */
             o = _check_create(ply_bts_box, _config->random, NULL, _random_state_changed, inst);
             elm_box_pack_end(ply_bts_box, o);
          }

        _media_length_update(inst, NULL);
        _media_position_update(inst, NULL);
        if (inst->cur_playlist_item)
          {
             elm_object_text_set(inst->play_song_lb, inst->cur_playlist_item->title);
             if (inst->cur_playlist_item->thumbnail_path)
                elm_image_file_set(inst->pl_img, inst->cur_playlist_item->thumbnail_path, NULL);
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
_instance_delete(Instance *inst)
{
   if (inst->o_icon) evas_object_del(inst->o_icon);
   if (inst->main_box) evas_object_del(inst->main_box);

   free(inst);
}

int main(int argc, char **argv)
{
   Instance *inst;

   eina_init();
   ecore_init();
   ecore_con_init();
   efreet_init();
   emotion_init();
   elm_init(argc, argv);

   _config_load();
   inst = _instance_create();

   Eo *win = elm_win_add(NULL, "Music", ELM_WIN_BASIC);
   inst->main = win;

   Eo *bg = elm_bg_add(win);
   evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_win_resize_object_add(win, bg);
   evas_object_show(bg);

   Eo *o = elm_box_add(win);
   evas_object_size_hint_align_set(o, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_show(o);
   elm_win_resize_object_add(win, o);
   efl_wref_add(o, &inst->main_box);

   evas_object_resize(win, 480, 480);
   evas_object_show(win);

   ecore_event_handler_add(ECORE_EXE_EVENT_ERROR, _exe_error_cb, inst);
   ecore_event_handler_add(ECORE_EXE_EVENT_DATA, _exe_output_cb, inst);
   ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _exe_end_cb, inst);

   srand(time(NULL));

   _box_update(inst);

   elm_run();

   _instance_delete(inst);
   elm_shutdown();
   emotion_shutdown();
   ecore_con_shutdown();
   ecore_shutdown();
   eina_shutdown();
   return 0;
}
