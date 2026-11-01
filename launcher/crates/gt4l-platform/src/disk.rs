//! Free space of a volume, for the setup copy of the disc image (checked before the copy starts).

use std::path::Path;

/// Free bytes available to the user on the volume that holds `folder` (or its nearest existing parent).
/// `None` when it cannot be read; setup then copies and reports a full disk as an error if it happens.
pub fn free_space(folder: &Path) -> Option<u64> {
    let existing = folder.ancestors().find(|candidate| candidate.exists())?;
    free_space_of(existing)
}

#[cfg(windows)]
fn free_space_of(existing: &Path) -> Option<u64> {
    use std::os::windows::ffi::OsStrExt;
    use windows::core::PCWSTR;
    use windows::Win32::Storage::FileSystem::GetDiskFreeSpaceExW;
    let wide: Vec<u16> = existing.as_os_str().encode_wide().chain(std::iter::once(0)).collect();
    let mut available = 0u64;
    // SAFETY: `wide` is a NUL-terminated UTF-16 path that outlives the call; the out pointer is a local u64.
    unsafe { GetDiskFreeSpaceExW(PCWSTR(wide.as_ptr()), Some(&mut available), None, None) }.ok()?;
    Some(available)
}

/// macOS and other Unix systems: `statvfs`, the blocks available to an unprivileged user.
#[cfg(unix)]
fn free_space_of(existing: &Path) -> Option<u64> {
    use std::os::unix::ffi::OsStrExt;
    let path = std::ffi::CString::new(existing.as_os_str().as_bytes()).ok()?;
    let mut stats: libc::statvfs = unsafe { std::mem::zeroed() };
    // SAFETY: `path` is NUL-terminated and outlives the call; `stats` is a local the call fills.
    if unsafe { libc::statvfs(path.as_ptr(), &mut stats) } != 0 {
        return None;
    }
    Some(stats.f_bavail as u64 * stats.f_frsize as u64)
}

#[cfg(not(any(windows, unix)))]
fn free_space_of(_existing: &Path) -> Option<u64> {
    None
}

#[cfg(test)]
mod tests {
    use super::free_space;

    #[test]
    fn the_temp_folder_has_some_free_space_and_missing_children_use_their_parent() {
        let temp = std::env::temp_dir();
        let direct = free_space(&temp);
        if cfg!(windows) {
            assert!(direct.is_some_and(|bytes| bytes > 0));
            assert!(free_space(&temp.join("does-not-exist").join("disc")).is_some());
        }
    }
}
