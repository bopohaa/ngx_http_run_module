use std::ptr::copy;
use std::str::{from_utf8};

#[no_mangle]
pub extern fn hello_rust(input: *const u8, input_size: u32, output:*mut u8, output_size:u32) -> i32 {
	if output.is_null() {
		return 0;
	}
	let mut data = match unsafe {
		let slice = std::slice::from_raw_parts(input, input_size as usize);
		from_utf8(slice)
	}
	{
		Err(_e) => return 0,
		Ok(v) => v.to_owned(),
	};
	data.push_str(", world!");

	if data.len()>output_size as usize {
		return -(data.len() as i32);
	}

	unsafe{
		copy(data.as_ptr(), output, data.len());
	}
	data.len() as i32
}
