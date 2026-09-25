//! Raw, unsafe bindings to the USVFS exception barrier.
//!
//! Callers own pointer validity, string termination, session lifetime, serialization,
//! and Windows handle ownership. The barrier catches C++ exceptions; it does not
//! make invalid pointers or concurrent session access safe. Safe runtime and process
//! facades belong in the consumer.
#![cfg(windows)]
#![allow(non_camel_case_types, non_snake_case, non_upper_case_globals, dead_code, clippy::all)]

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

#[cfg(test)]
mod tests {
	use super::mods_usvfs_clear_bypasses;
	use super::mods_usvfs_close;
	use super::mods_usvfs_launch;
	use super::mods_usvfs_link_directory;
	use super::mods_usvfs_link_file;
	use super::mods_usvfs_open;
	use std::hint::black_box;

	#[test]
	fn barrier_symbols_link() {
		// Keep relocations to every owned shim export without opening a controller
		// or launching a process. Compilation alone can leave the archive unused.
		black_box([
			mods_usvfs_open as *const (),
			mods_usvfs_close as *const (),
			mods_usvfs_clear_bypasses as *const (),
			mods_usvfs_link_directory as *const (),
			mods_usvfs_link_file as *const (),
			mods_usvfs_launch as *const (),
		]);
	}
}
