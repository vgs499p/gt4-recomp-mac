//! Moves files and folders to the Recycle Bin (SHFileOperationW with FOF_ALLOWUNDO), so a delete
//! in the launcher can always be undone from the bin. On macOS the same call moves them to the Trash.

use std::path::Path;

/// Moves one file or folder to the Recycle Bin without confirmation dialogs.
#[cfg(windows)]
pub fn move_to_recycle_bin(path: &Path) -> Result<(), String> {
    use std::os::windows::ffi::OsStrExt;
    use windows::core::PCWSTR;
    use windows::Win32::Foundation::HWND;
    use windows::Win32::UI::Shell::{
        SHFileOperationW, FOF_ALLOWUNDO, FOF_NOCONFIRMATION, FOF_NOERRORUI, FOF_SILENT, FO_DELETE, SHFILEOPSTRUCTW,
    };

    // pFrom is a double-null-terminated list of paths.
    let mut wide: Vec<u16> = path.as_os_str().encode_wide().collect();
    wide.push(0);
    wide.push(0);
    let mut operation = SHFILEOPSTRUCTW {
        hwnd: HWND(std::ptr::null_mut()),
        wFunc: FO_DELETE,
        pFrom: PCWSTR(wide.as_ptr()),
        fFlags: (FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI).0 as u16,
        ..Default::default()
    };
    // SAFETY: `operation` and the buffer behind `pFrom` live until the call returns.
    let result = unsafe { SHFileOperationW(&mut operation) };
    if result != 0 {
        return Err(format!("could not move {} to the Recycle Bin (error {result:#x})", path.display()));
    }
    if operation.fAnyOperationsAborted.as_bool() {
        return Err(format!("the delete of {} was cancelled", path.display()));
    }
    Ok(())
}

/// macOS: moves the item to the Trash (`NSFileManager trashItemAtURL`), where Finder can put it back.
#[cfg(target_os = "macos")]
pub fn move_to_recycle_bin(path: &Path) -> Result<(), String> {
    use objc2_foundation::{NSFileManager, NSString, NSURL};

    let text = path.to_str().ok_or_else(|| format!("not a UTF-8 path: {}", path.display()))?;
    let url = NSURL::fileURLWithPath(&NSString::from_str(text));
    let manager = NSFileManager::defaultManager();
    manager
        .trashItemAtURL_resultingItemURL_error(&url, None)
        .map_err(|error| format!("could not move {} to the Trash: {}", path.display(), error.localizedDescription()))
}

#[cfg(not(any(windows, target_os = "macos")))]
pub fn move_to_recycle_bin(path: &Path) -> Result<(), String> {
    Err(format!("the Recycle Bin is only available on Windows: {}", path.display()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_missing_path_is_refused_and_nothing_is_deleted() {
        let missing = std::env::temp_dir().join(format!("gt4l-recycle-missing-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&missing);
        assert!(move_to_recycle_bin(&missing).is_err());
    }

    #[test]
    #[cfg(not(any(windows, target_os = "macos")))]
    fn off_windows_every_delete_is_refused() {
        let path = std::env::temp_dir();
        let error = move_to_recycle_bin(&path).unwrap_err();
        assert!(error.contains("only available on Windows"));
    }
}
