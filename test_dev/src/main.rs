use std::fs::OpenOptions;
use std::io::{Read, Write, Seek, SeekFrom};
use std::os::unix::io::AsRawFd;
use std::sync::{Arc, RwLock};
use std::thread;
use std::ptr;
use std::slice;
use rand::Rng;
use libc::{open, O_DIRECT, O_RDWR, close};
use libc::posix_memalign;
use libc::c_void;

const DEVICE_PATH: &str = "/dev/csl";
const SECTOR_SIZE: usize = 512;

fn open_direct(path: &str) -> i32 {
    unsafe {
        let c_path = std::ffi::CString::new(path).unwrap();
        open(c_path.as_ptr(), O_DIRECT | O_RDWR, 0)
    }
}

fn aligned_alloc(size: usize, alignment: usize) -> *mut u8 {
    let mut ptr: *mut c_void = ptr::null_mut();
    unsafe {
        posix_memalign(&mut ptr, alignment, size);
    }
    ptr as *mut u8
}

fn write_random_data(fd: i32, offset: u64, size: usize) -> Vec<u8> {
    let mut rng = rand::thread_rng();
    let data: Vec<u8> = (0..size).map(|_| rng.gen()).collect();

    let buf = aligned_alloc(size, SECTOR_SIZE);
    unsafe {
        ptr::copy_nonoverlapping(data.as_ptr(), buf, size);
        libc::pwrite(fd, buf as *const c_void, size, offset as i64);
        libc::free(buf as *mut c_void);
    }

    data
}

fn read_data(fd: i32, offset: u64, size: usize) -> Vec<u8> {
    let buf = aligned_alloc(size, SECTOR_SIZE);
    unsafe {
        libc::pread(fd, buf as *mut c_void, size, offset as i64);
        let data = slice::from_raw_parts(buf, size).to_vec();
        libc::free(buf as *mut c_void);
        data
    }
}

fn test_read_write(fd: i32) {
    let offset = 0;

    // Write random data to the device
    let write_data = write_random_data(fd, offset, SECTOR_SIZE);

    // Read data back from the device
    let read_data = read_data(fd, offset, SECTOR_SIZE);

    // Verify the data
    assert_eq!(write_data, read_data);
}

fn test_multithread_sync(fd: i32) {
    let fd = Arc::new(RwLock::new(fd));
    let mut handles = vec![];

    for i in 0..4 {
        let fd = Arc::clone(&fd);
        let handle = thread::spawn(move || {
            for _ in 0..100 {
                let fd = fd.write().unwrap();
                let offset = (i * SECTOR_SIZE) as u64;
                let write_data = write_random_data(*fd, offset, SECTOR_SIZE);
                let read_data = read_data(*fd, offset, SECTOR_SIZE);
                assert_eq!(write_data, read_data);
            }
        });
        handles.push(handle);
    }

    for handle in handles {
        handle.join().unwrap();
    }
}

fn main() {
    let fd = open_direct(DEVICE_PATH);
    if fd < 0 {
        eprintln!("Failed to open device with O_DIRECT");
        return;
    }

    test_read_write(fd);
    test_multithread_sync(fd);

    unsafe { close(fd) };

    println!("All tests passed.");
}
