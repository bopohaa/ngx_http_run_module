# Executing exported functions in dynamically compiled libraries (.so) from Nginx

Adds the ability to perform a specially designed export function from a dynamically compiled library

## Build

To link statically against nginx, cd to nginx source directory and execute:

    ./configure --add-module=/path/to/ngx_http_run_module

To compile as a dynamic module (nginx 1.9.11+), use:
  
	./configure --add-dynamic-module=/path/to/ngx_http_run_module

In this case, the `load_module` directive should be used in nginx.conf to load the module.

## Configuration

Set the library of the function that will be called

### run_lib
* **syntax**: `run_lib /path/to/library.so`
* **default**: `none`
* **context**: `main`

Set the name of the called function

### run_func
* **syntax**: `run_func exported_function_name`
* **default**: `none`
* **context**: `main, server, location`

## Execution

Execute the specified function with the specified value and record the result in a new variable (the value can be a string or the name of a variable)

### run_func
* **syntax**: `run $output_variable_name $input_variable_name|raw_value [optional_buffer_size=4095]`
* **context**: `location`

## Exported function type
`int32_t function_name(void* input, uint32_t input_size, void* output, uint32_t output_size)`
Where
* **input**: raw pointer to input data from `run` directive
* **input_size**: size in bytes of input data
* **output**: raw pointer to output data buffer from `run` directive
* **output_size**: max size in bytes of output data buffer
* **_return value_**: size result in bytes stored in output data buffer, if less 0 then this is abs a required size

## Sample configuration
```
http {
	run_lib my_rust_lib.so;
	
	server {
		run_func hello_rust
		location /hello {
			run $body Hello;
			return 200 $body;
		}
	}
```
## Copyright & License

All code in this project is released under the [BSD license](https://github.com/bopohaa/ngx_http_run_module/blob/master/LICENSE) unless a different license for a particular library is specified in the applicable library path. 

Copyright © 2019 Nikolay Vorobev.
All rights reserved.
