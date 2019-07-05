#pragma once
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
	ngx_str_t name;
	size_t args_cnt;
	ngx_str_t args[NGX_CONF_MAX_ARGS];
} run_fn_conf_t;

typedef struct
{
	void* entrypoint;

	ngx_str_t lib;
	run_fn_conf_t init;
	run_fn_conf_t dispose;
} ngx_run_conf_t;

ngx_module_t ngx_core_run_module;
