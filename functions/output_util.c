/* Copyright(C) 2015 Naoya Murakami <naoya@createfield.com>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License version 2.1 as published by the Free Software Foundation.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301  USA
*/

#include <string.h>
#include <groonga/plugin.h>

#ifdef __GNUC__
# define GNUC_UNUSED __attribute__((__unused__))
#else
# define GNUC_UNUSED
#endif

static grn_obj *
func_output_copy(grn_ctx *ctx, GNUC_UNUSED int nargs, GNUC_UNUSED grn_obj **args,
                 GNUC_UNUSED grn_user_data *user_data)
{
  grn_obj *output = args[0];
  grn_obj *key_str = args[1];
  grn_obj *column = args[2];
  grn_obj *table;
  grn_id id;
  if (nargs < 3) {
    return output;
  }

  if (output && GRN_TEXT_LEN(output) > 0) {
    table = grn_ctx_at(ctx, column->header.domain);
    id = grn_table_get(ctx, table, GRN_TEXT_VALUE(key_str), GRN_TEXT_LEN(key_str));
    if (id) {
      grn_obj_set_value(ctx, column, id, output, GRN_OBJ_SET); 
    }
    if (table) {
      grn_obj_unlink(ctx, table);
    }
  }
  return output;
}

static grn_obj *
func_output_group(grn_ctx *ctx, GNUC_UNUSED int nargs, GNUC_UNUSED grn_obj **args,
                  grn_user_data *user_data)
{
  grn_obj *output = args[0];
  grn_obj *target_column = args[1];
  grn_obj *group_column_str = args[2];
  grn_obj *filter = args[3];
  grn_obj *table;
  grn_obj *res = NULL;
  grn_obj *result;

  result = grn_plugin_proc_alloc(ctx, user_data, GRN_DB_SHORT_TEXT, GRN_OBJ_VECTOR);
  if (nargs < 3) {
    return result;
  }
  table = grn_ctx_at(ctx, target_column->header.domain);

  if (target_column->header.type != GRN_COLUMN_INDEX) {
    grn_column_index(ctx, target_column, GRN_OP_MATCH, &target_column, 1, NULL);
  }
  if (target_column->header.type != GRN_COLUMN_INDEX) {
    return result;
  }

  res = grn_table_create(ctx, NULL, 0, NULL,
                          GRN_TABLE_HASH_KEY|GRN_OBJ_WITH_SUBREC,
                          table, NULL);
  if (res) {
    grn_search_optarg search_options;
    grn_rc rc;
    memset(&search_options, 0, sizeof(grn_search_optarg));
    search_options.mode = GRN_OP_EXACT;
    search_options.similarity_threshold = 0;
    search_options.max_interval = 0;
    search_options.weight_vector = NULL;
    search_options.vector_size = 0;
    search_options.proc = NULL;
    search_options.max_size = 0;
    search_options.scorer = NULL;
    rc = grn_obj_search(ctx, target_column, output, res, GRN_OP_OR, &search_options);
    if (rc != GRN_SUCCESS) {
      GRN_PLUGIN_LOG(ctx, GRN_LOG_ERROR, "[output_group] failed index search");
      if (table) {
        grn_obj_unlink(ctx, table);
      }
      if (res) {
        grn_obj_unlink(ctx, res);
      }
      return result;
    }
  }
  if (filter && GRN_TEXT_LEN(filter)){
    grn_obj *v, *cond;

    GRN_EXPR_CREATE_FOR_QUERY(ctx, table, cond, v);
    grn_expr_parse(ctx, cond,
                   GRN_TEXT_VALUE(filter),
                   GRN_TEXT_LEN(filter),
                   NULL,
                   GRN_OP_MATCH,
                   GRN_OP_AND,
                   GRN_EXPR_SYNTAX_SCRIPT);
    grn_table_select(ctx, table, cond, res, GRN_OP_AND);
  }

  {
    grn_table_group_result g = {NULL, 0, 0, 1, GRN_TABLE_GROUP_CALC_COUNT, 0, 0, NULL};
    grn_table_sort_key *keys = NULL;
    unsigned int n_keys;

    keys = grn_table_sort_key_from_str(ctx,
                                       GRN_TEXT_VALUE(group_column_str),
                                       GRN_TEXT_LEN(group_column_str),
                                       res, &n_keys);
    grn_table_group(ctx, res, keys, n_keys, &g, 1);

    grn_obj *sorted;
    if ((sorted = grn_table_create(ctx, NULL, 0, NULL, GRN_OBJ_TABLE_NO_KEY, NULL, g.table))) {
      uint32_t ngkeys;
      grn_table_sort_key *gkeys;
      const char *sortby_val = "-_nsubrecs";
      unsigned int sortby_len = sizeof("-_nsubrecs") - 1;
      int offset = 0;
      int limit = 10;

      if ((gkeys = grn_table_sort_key_from_str(ctx, sortby_val, sortby_len, g.table, &ngkeys))) {
        grn_table_sort(ctx, g.table, offset, limit, sorted, gkeys, ngkeys);
        {
           grn_table_cursor *tc;
           grn_obj buf;
           GRN_TEXT_INIT(&buf, 0);
           if ((tc = grn_table_cursor_open(ctx, sorted, NULL, 0, NULL, 0, 0, -1, GRN_CURSOR_BY_ID))) {
             grn_id id;
             grn_obj *column = grn_obj_column(ctx, sorted,
                                              GRN_COLUMN_NAME_KEY,
                                              GRN_COLUMN_NAME_KEY_LEN);
             while ((id = grn_table_cursor_next(ctx, tc))) {
               GRN_BULK_REWIND(&buf);
               grn_obj_get_value(ctx, column, id, &buf);
               if (GRN_TEXT_LEN(&buf)) {
                 grn_vector_add_element(ctx, result,
                                        GRN_TEXT_VALUE(&buf), GRN_TEXT_LEN(&buf),
                                        0, GRN_DB_SHORT_TEXT);
               }
             }
             grn_obj_unlink(ctx, column);
             grn_table_cursor_close(ctx, tc);
           }
           grn_obj_unlink(ctx, &buf);
         }
         grn_table_sort_key_close(ctx, gkeys, ngkeys);
       }
       if (sorted) {
         grn_obj_unlink(ctx, sorted);
       }
    }
    grn_table_sort_key_close(ctx, keys, n_keys);
    if (g.table) {
      grn_obj_unlink(ctx, g.table);
    }
  }
  if (res) {
    grn_obj_unlink(ctx, res);
  }
  if (table) {
    grn_obj_unlink(ctx, table);
  }

  return result;
}

grn_rc
GRN_PLUGIN_INIT(GNUC_UNUSED grn_ctx *ctx)
{
  return GRN_SUCCESS;
}

grn_rc
GRN_PLUGIN_REGISTER(grn_ctx *ctx)
{
  grn_proc_create(ctx, "output_group", -1, GRN_PROC_FUNCTION,
                  func_output_group, NULL, NULL, 0, NULL);

  grn_proc_create(ctx, "output_copy", -1, GRN_PROC_FUNCTION,
                  func_output_copy, NULL, NULL, 0, NULL);


  return ctx->rc;
}

grn_rc
GRN_PLUGIN_FIN(GNUC_UNUSED grn_ctx *ctx)
{
  return GRN_SUCCESS;
}
