use std::ptr::copy;
use std::str::{from_utf8};

#[no_mangle]
pub extern fn hello_rust(input_count:u32, input_params: * const *const u8, input_sizes: *const u32, output:*mut u8, output_size:u32) -> i32 {
	if output.is_null() {
		return 0;
	}

	if input_count < 2 {
		return 0;
	}

	let input_params = unsafe{std::slice::from_raw_parts(input_params, input_count as usize)};
	let input_sizes = unsafe{std::slice::from_raw_parts(input_sizes, input_count as usize)};

	let hello = unsafe {
		let slice = std::slice::from_raw_parts(input_params[0], input_sizes[0] as usize);
		from_utf8(slice)
	}.unwrap_or("");
	let world = unsafe {
		let slice = std::slice::from_raw_parts(input_params[1], input_sizes[1] as usize);
		from_utf8(slice)
	}.unwrap_or("");

	let required = hello.len() + world.len();
	if required > output_size as usize {
		return -(required as i32);
	}

	unsafe{
		copy(hello.as_ptr(), output, hello.len());
		copy(world.as_ptr(), output.add(hello.len()),world.len())
	}

	required as i32
}