use bindgen::Builder;
use cc::Build;
use std::env;
use std::error::Error;
use std::path::PathBuf;

fn main() -> Result<(), Box<dyn Error>> {
	println!("cargo:rerun-if-changed=native/barrier.h");
	println!("cargo:rerun-if-changed=native/barrier.cpp");
	if env::var("CARGO_CFG_TARGET_OS")? != "windows" {
		return Ok(());
	}
	let target = env::var("TARGET")?;
	if target != "x86_64-pc-windows-msvc" && target != "i686-pc-windows-msvc" {
		return Err("usvfs supports only x86/x64 MSVC targets".into());
	}
	let upstream = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?).join("../../include");
	let output = PathBuf::from(env::var("OUT_DIR")?);
	let bindings = Builder::default()
		.header_contents(
			"mods_usvfs_bindings.hpp",
			"#include \"barrier.h\"\n#include <usvfs/usvfs.h>\n",
		)
		.clang_args(["-x", "c++", "-std=c++20"])
		.clang_arg(format!("--target={target}"))
		.clang_arg(format!("-I{}", upstream.display()))
		.clang_arg("-Inative")
		.clang_arg("-includeWindows.h")
		.allowlist_function("mods_usvfs_.*")
		.allowlist_var("LINKFLAG_(CREATETARGET|RECURSIVE)|MODS_CLEANUP_TIMEOUT_MS")
		.opaque_type("usvfsParameters")
		.opaque_type("ModsUsvfs")
		.derive_default(true)
		.layout_tests(false)
		.parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
		.generate()?;
	bindings.write_to_file(output.join("bindings.rs"))?;
	Build::new()
		.cpp(true)
		.file("native/barrier.cpp")
		.include(upstream)
		.flag("/std:c++20")
		.flag("/EHsc")
		.compile("mods_usvfs_barrier");

	Ok(())
}
