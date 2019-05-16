/*
BSD 2-Clause License

Copyright (c) 2019, Nikolay Vorobev
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
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

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <dlfcn.h>

#define BUFFER_SIZE_MIN NGX_MAX_ALLOC_FROM_POOL
#define BUFFER_SIZE_MAX 65536*16

static void* ngx_http_run_create_loc_conf(ngx_conf_t* cf);
static char* ngx_http_run_merge_loc_conf(ngx_conf_t* cf, void* parent, void* child);

static char* ngx_main_run_lib_cmd(ngx_conf_t* cf, ngx_command_t* cmd, void* conf);
static char* ngx_http_run_cmd(ngx_conf_t* cf, ngx_command_t* cmd, void* conf);

typedef int32_t(*run_func_ptr)(void* input, uint32_t input_size, void* output, uint32_t output_size);

typedef struct {
	ngx_str_t lib;
	void* entrypoint;
	ngx_str_t func;
	run_func_ptr run;
} ngx_http_run_loc_conf_t;

typedef struct {
	ngx_int_t input_variable_index;
	ngx_str_t or_raw_data;
	int32_t min_buffer_size;
} run_func_conf_t;

static ngx_command_t ngx_http_run_commands[] = {

	{ ngx_string("run_lib"),
	  NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
	  ngx_main_run_lib_cmd,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  0,
	  NULL},
	{ ngx_string("run_func"),
	  NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
	  ngx_conf_set_str_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_run_loc_conf_t, func),
	  NULL},
	{ ngx_string("run"),
	  NGX_HTTP_LOC_CONF | NGX_CONF_TAKE23,
	  ngx_http_run_cmd,
	  0,
	  0,
	  NULL},

	ngx_null_command
};

static ngx_http_module_t ngx_http_run_module_ctx = {
	NULL, /* preconfiguration */
	NULL, /* postconfiguration */

	NULL, /* create main configuration */
	NULL, /* init main configuration */

	NULL, /* create server configuration */
	NULL, /* merge server configuration */

	ngx_http_run_create_loc_conf, /* create location configuration */
	ngx_http_run_merge_loc_conf /* merge location configuration */
};

ngx_module_t ngx_http_run_module = {
	NGX_MODULE_V1,
	&ngx_http_run_module_ctx, /* module context */
	ngx_http_run_commands, /* module directives */
	NGX_HTTP_MODULE, /* module type */
	NULL, /* init master */
	NULL, /* init module */
	NULL, /* init process */
	NULL, /* init thread */
	NULL, /* exit thread */
	NULL, /* exit process */
	NULL, /* exit master */
	NGX_MODULE_V1_PADDING
};

static void* ngx_http_run_create_loc_conf(ngx_conf_t* cf) {
	ngx_http_run_loc_conf_t* conf;

	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_run_loc_conf_t));
	if (conf == NULL)
		return NGX_CONF_ERROR;

	conf->run = NGX_CONF_UNSET_PTR;
	conf->entrypoint = NGX_CONF_UNSET_PTR;

	return conf;
}

static char* ngx_http_run_merge_loc_conf(ngx_conf_t * cf, void* parent, void* child) {
	ngx_http_run_loc_conf_t* prev = parent;
	ngx_http_run_loc_conf_t* conf = child;
	ngx_conf_merge_ptr_value(conf->entrypoint, prev->entrypoint, NGX_CONF_UNSET_PTR);
	ngx_conf_merge_ptr_value(conf->run, prev->run, NGX_CONF_UNSET_PTR);
	ngx_conf_merge_str_value(conf->lib, prev->lib, NULL);
	ngx_conf_merge_str_value(conf->func, prev->func, NULL);

	if (conf->entrypoint != NGX_CONF_UNSET_PTR && conf->func.data != NULL && conf->func.len > 0) {
		char tmp[2049];
		ngx_memzero(tmp, 2049);
		ngx_memcpy(tmp, conf->func.data, conf->func.len);

		run_func_ptr run = ngx_dlsym(conf->entrypoint, tmp);
		if (run == 0) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
				ngx_dlsym_n " \"%s\", \"%s\" failed (%s)",
				tmp, "ngx_modules", ngx_dlerror());
			return NGX_CONF_ERROR;
		}

		conf->run = run;
	}

	return NGX_CONF_OK;
}

static void ngx_http_run_lib_unload(void* data)
{
	void* handle = data;

	if (ngx_dlclose(handle) != 0) {
		ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
			ngx_dlclose_n " failed (%s)", ngx_dlerror());
	}
}
static char* ngx_main_run_lib_cmd(ngx_conf_t * cf, ngx_command_t * cmd, void* conf) {
	if (conf == NULL)
		return NGX_CONF_ERROR;

	ngx_http_run_loc_conf_t * c = (ngx_http_run_loc_conf_t*)conf;
	if (c->entrypoint != NGX_CONF_UNSET_PTR)
		return "is duplicate";

	ngx_pool_cleanup_t * cln = ngx_pool_cleanup_add(cf->cycle->pool, 0);
	if (cln == NULL)
		return NGX_CONF_ERROR;

	ngx_str_t * values;
	values = cf->args->elts;

	if (values[1].len > 2048)
		return NGX_CONF_ERROR;

	char tmp[2049];
	ngx_memzero(tmp, 2049);
	ngx_memcpy(tmp, values[1].data, values[1].len);
	void* handle = ngx_dlopen(tmp);
	if (handle == 0) {
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
			ngx_dlopen_n " \"%s\" failed (%s)",
			tmp, ngx_dlerror());
		return NGX_CONF_ERROR;
	}
	cln->data = handle;
	cln->handler = ngx_http_run_lib_unload;

	c->entrypoint = handle;
	c->lib.data = ngx_pstrdup(cf->pool, &values[1]);
	c->lib.len = values[1].len;

	return NGX_CONF_OK;
}

ngx_int_t ngx_http_run_get_variable(ngx_http_request_t * r, ngx_http_variable_value_t * v, uintptr_t data) {
	ngx_http_run_loc_conf_t* c = ngx_http_get_module_loc_conf(r, ngx_http_run_module);
	if (c == NULL)
		return NGX_ERROR;
	if (c->run == NULL)
		return NGX_ERROR;

	void* result;
	void* input;
	size_t inputSize;

	run_func_conf_t * loc = (run_func_conf_t*)data;
	if (loc->input_variable_index == NGX_ERROR) {
		input = loc->or_raw_data.data;
		inputSize = loc->or_raw_data.len;
	}
	else {
		ngx_http_variable_value_t* v = ngx_http_get_indexed_variable(r, loc->input_variable_index);
		if (v == NULL || v->not_found)
			return NGX_ERROR;
		input = v->data;
		inputSize = v->len;
	}

	size_t size = loc->min_buffer_size;
	result = ngx_pnalloc(r->pool, size);
	if (result == NULL)
		return NGX_ERROR;

	int32_t resultSize = c->run(input, inputSize, result, size);
	if (resultSize < 0) {
		ngx_pfree(r->pool, result);
		size = 1 + ~resultSize;
		result = ngx_pnalloc(r->pool, size);
		if (result == NULL)
			return NGX_ERROR;
		resultSize = c->run(input, inputSize, result, size);
		if (resultSize < 0)
			return NGX_ERROR;
	}

	if (resultSize == 0) {
		v->not_found = 1;
		v->no_cacheable = 1;
		v->valid = 0;
		v->data = NULL;
		v->len = 0;
		return NGX_OK;
	}
	v->valid = 1;
	v->no_cacheable = 0;
	v->not_found = 0;
	v->data = result;
	v->len = resultSize;
	return NGX_OK;
}

static char* ngx_http_run_cmd(ngx_conf_t * cf, ngx_command_t * cmd, void* conf)
{
	ngx_str_t* values = cf->args->elts;

	if (values[1].data[0] != '$') {
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
			"invalid variable name \"%V\"", &values[1]);
		return NGX_CONF_ERROR;
	}
	values[1].len--;
	values[1].data++;

	run_func_conf_t* loc;
	loc = ngx_pcalloc(cf->pool, sizeof(run_func_conf_t));
	if (loc == NULL)
		return NGX_CONF_ERROR;

	loc->min_buffer_size = cf->args->nelts == 4 ? ngx_atoi(values[3].data, values[3].len) : ((int32_t)BUFFER_SIZE_MIN);
	if (loc->min_buffer_size == NGX_ERROR)
		return "wrong type of third parameter";

	if (values[2].data[0] == '$') {
		values[2].len--;
		values[2].data++;
		loc->input_variable_index = ngx_http_get_variable_index(cf, &values[2]);
		if (loc->input_variable_index == NGX_ERROR)
			return "input variable not found";
	}
	else {
		loc->input_variable_index = NGX_ERROR;
		loc->or_raw_data = values[2];
	}


	ngx_http_variable_t* v = ngx_http_add_variable(cf, &values[1], NGX_HTTP_VAR_WEAK);
	if (v == NULL)
		return NGX_CONF_ERROR;

	v->get_handler = ngx_http_run_get_variable;
	v->data = (uintptr_t)loc;

	return NGX_CONF_OK;
}
