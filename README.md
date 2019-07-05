# Executing exported functions in dynamically compiled libraries (.so) from Nginx

Adds the ability to perform a specially designed export function from a dynamically compiled library

## Build

To link statically against nginx, cd to nginx source directory and execute:

    ./configure --add-module=/path/to/ngx_run_module

To compile as a dynamic module (nginx 1.9.11+), use:
  
	./configure --add-dynamic-module=/path/to/ngx_run_module

In this case, the `load_module` directive should be used in nginx.conf to load the module `ngx_core_run_module.so`.

## Configuration

Set the library of the function that will be called

### run_lib
* **syntax**: `run_lib /path/to/library.so`
* **default**: `none`
* **context**: `main`

Specify the size of the buffer to record the execution result

### run_buf
* **syntax**: `run_buf buffer_size_in_bytes`
* **default**: `NGX_MAX_ALLOC_FROM_POOL`
* **context**: `main, server, location`
Where
* **buffer_size_in_bytes**: buffer size in bytes

## Execution

Execute the specified initialization function (execuite once per worker process init) with the specified values

### run_init
* **syntax**: `run_init exported_function_name [value...]`
* **default**: `none`
* **context**: `main`
Where
* **exported_function_name**: exported function name in our dynlib
* **value**: values that will be passed to the called function (several parameters can be passed)

Execute the specified exit function (execuite once per worker process exit) with the specified values

### run_exit
* **syntax**: `run_exit exported_function_name [value...]`
* **default**: `none`
* **context**: `main`
Where
* **exported_function_name**: exported function name in our dynlib
* **value**: values that will be passed to the called function (several parameters can be passed)

Execute the specified function with the specified value and record the result in a new variable (the value can be a string or the name of a variable)

### run_func
* **syntax**: `run exported_function_name $output_variable_name [$input_variable_name|or_raw_value...]`
* **context**: `location`
Where
* **exported_function_name**: exported function name in our dynlib
* **$output_variable_name**: variable name nginx to record the result of the function
* **$input_variable_name|or_raw_value**: values that will be passed to the called function (several parameters can be passed)

## Exported function types

### For `run` command
`int32_t run_func_name (uint32_t input_count, void** input, uint32_t* input_size, void* output, uint32_t output_size)`
Where
* **input_count**: number of parameters passed
* **input**: raw pointer to the list of passed parameters
* **input_size**: raw pointer to the list of sizes of the passed parameters
* **output**: raw pointer to output data buffer from `run` directive
* **output_size**: max size in bytes of output data buffer
* **_return value_**: size result in bytes stored in output data buffer, if less than 0, then this is the abs required size (it will be repeated with the requested size of the outgoing buffer)

### For `run_init` or `run_exit` commands
`void run_func_name (uint32_t input_count, void** input, uint32_t* input_size)`
Where
* **input_count**: number of parameters passed
* **input**: raw pointer to the list of passed parameters
* **input_size**: raw pointer to the list of sizes of the passed parameters

## Sample configuration
```
load_module /usr/lib64/nginx/modules/ngx_core_run_module.so;

run_lib my_rust_lib.so;
run_init hello_rust_init;
run_exit hello_rust_exit;

http {
	server {
		location /hello {
			run hello_rust $body Hello World;
			return 200 $body;
		}
	}
```
## Copyright & License

All code in this project is released under the [BSD license](https://github.com/bopohaa/ngx_http_run_module/blob/master/LICENSE) unless a different license for a particular library is specified in the applicable library path. 

Copyright © 2019 Nikolay Vorobev.
All rights reserved.
