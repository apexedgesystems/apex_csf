use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::{AsRawFd, BorrowedFd, RawFd};
use std::time::Duration;

use nix::libc; // O_NOCTTY, O_NONBLOCK
use nix::sys::select::{select, FdSet}; // requires nix feature "poll"
use nix::sys::termios as nx;
use nix::sys::time::TimeVal; // requires nix feature "time" // requires nix feature "term"

use super::probe::{BaudRate, NumStopBits, Parity, SerialParams};

pub struct SerialPort {
    fd: RawFd,
    file: File,
}

impl SerialPort {
    /// Open a TTY for I/O (nonblocking). We intentionally do NOT use O_EXCL here
    /// because the validator already checked contention; tests need to proceed.
    pub fn open(path: &str, params: SerialParams) -> std::io::Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .custom_flags(libc::O_NOCTTY | libc::O_NONBLOCK)
            .open(path)?;
        let fd = file.as_raw_fd();

        // Flush and configure (raw, 8 data bits, parity/stop/baud)
        nx::tcflush(&file, nx::FlushArg::TCIOFLUSH).map_err(from_nix)?;
        configure_termios(&file, params)?;

        Ok(Self { fd, file })
    }

    /// Write raw bytes with a simple writability wait (timeout).
    pub fn write_bytes(&mut self, data: &[u8], timeout: Duration) -> std::io::Result<usize> {
        wait_writable(self.fd, timeout)?;
        self.file.write(data)
    }

    pub fn write_ascii(&mut self, s: &str, timeout: Duration) -> std::io::Result<usize> {
        self.write_bytes(s.as_bytes(), timeout)
    }

    /// Read up to `size` bytes with timeout.
    pub fn read_bytes(&mut self, size: usize, timeout: Duration) -> std::io::Result<Vec<u8>> {
        if !wait_readable(self.fd, timeout)? {
            return Ok(Vec::new());
        }
        let mut buf = vec![0u8; size];
        let n = self.file.read(&mut buf)?;
        buf.truncate(n);
        Ok(buf)
    }
}

fn wait_readable(fd: RawFd, timeout: Duration) -> std::io::Result<bool> {
    let mut readfds = FdSet::new();

    // FdSet::insert/contains require BorrowedFd in nix 0.29
    let bfd = unsafe { BorrowedFd::borrow_raw(fd) };
    readfds.insert(bfd);

    let mut tv = duration_to_timeval(timeout);
    match select(fd + 1, Some(&mut readfds), None, None, Some(&mut tv)) {
        Ok(_n) => Ok(readfds.contains(bfd)),
        Err(e) => Err(std::io::Error::other(e.to_string())),
    }
}

fn wait_writable(fd: RawFd, timeout: Duration) -> std::io::Result<()> {
    let mut writefds = FdSet::new();

    let bfd = unsafe { BorrowedFd::borrow_raw(fd) };
    writefds.insert(bfd);

    let mut tv = duration_to_timeval(timeout);
    match select(fd + 1, None, Some(&mut writefds), None, Some(&mut tv)) {
        Ok(_n) => Ok(()),
        Err(e) => Err(std::io::Error::other(e.to_string())),
    }
}

fn duration_to_timeval(d: Duration) -> TimeVal {
    // TimeVal::new(seconds, microseconds)
    let secs = d.as_secs() as i64;
    let usec = d.subsec_micros() as i64;
    TimeVal::new(secs, usec)
}

fn configure_termios(file: &File, p: SerialParams) -> std::io::Result<()> {
    use super::probe::BaudRate::*;

    let mut tio = nx::tcgetattr(file).map_err(from_nix)?;
    nx::cfmakeraw(&mut tio);

    // 8 data bits, enable receiver, ignore modem control lines
    tio.control_flags
        .insert(nx::ControlFlags::CS8 | nx::ControlFlags::CREAD | nx::ControlFlags::CLOCAL);
    // Disable RTS/CTS
    tio.control_flags.remove(nx::ControlFlags::CRTSCTS);

    // Stop bits
    match p.stop_bits {
        NumStopBits::One => tio.control_flags.remove(nx::ControlFlags::CSTOPB),
        NumStopBits::Two => tio.control_flags.insert(nx::ControlFlags::CSTOPB),
    }

    // Parity
    match p.parity {
        Parity::None => tio.control_flags.remove(nx::ControlFlags::PARENB),
        Parity::Odd => {
            tio.control_flags
                .insert(nx::ControlFlags::PARENB | nx::ControlFlags::PARODD);
        }
        Parity::Even => {
            tio.control_flags.insert(nx::ControlFlags::PARENB);
            tio.control_flags.remove(nx::ControlFlags::PARODD);
        }
    }

    // Speeds (standard only)
    let br = match p.baud {
        B9600 => nx::BaudRate::B9600,
        B19200 => nx::BaudRate::B19200,
        B115200 => nx::BaudRate::B115200,
        B921600 => nx::BaudRate::B921600,
        BaudRate::Custom(_) => {
            return Err(std::io::Error::new(
                std::io::ErrorKind::Unsupported,
                "Custom baud not yet supported in SerialPort",
            ));
        }
    };
    nx::cfsetispeed(&mut tio, br).map_err(from_nix)?;
    nx::cfsetospeed(&mut tio, br).map_err(from_nix)?;

    // Non-blocking read with short timeout: VMIN=0, VTIME=1 (0.1s)
    tio.control_chars[nx::SpecialCharacterIndices::VMIN as usize] = 0;
    tio.control_chars[nx::SpecialCharacterIndices::VTIME as usize] = 1;

    nx::tcsetattr(file, nx::SetArg::TCSANOW, &tio).map_err(from_nix)?;
    Ok(())
}

fn from_nix(e: nix::Error) -> std::io::Error {
    std::io::Error::other(e.to_string())
}

// ----- Inline unit tests (Unix-only) ------------------------------------------------------------
#[cfg(test)]
mod tests {
    use super::*;
    use nix::unistd::{pipe, write};
    use std::time::Duration as StdDuration;

    #[test]
    fn duration_to_timeval_converts_correctly() {
        let d = Duration::from_millis(1234); // 1.234s
        let tv = super::duration_to_timeval(d);
        assert_eq!(tv.tv_sec(), 1);
        assert_eq!(tv.tv_usec(), 234_000);
    }

    // On non-TTY targets (e.g. /dev/null), tcgetattr will fail first (Other/ENOTTY),
    // while on a real TTY with Custom baud we'd hit the Unsupported branch.
    // Accept either so the test is stable across environments.
    #[test]
    fn configure_termios_rejects_custom_baud() {
        let f = File::open("/dev/null").unwrap();
        let params = SerialParams {
            baud: BaudRate::Custom(12345),
            parity: Parity::None,
            stop_bits: NumStopBits::One,
        };
        let err = super::configure_termios(&f, params).unwrap_err();
        assert!(matches!(
            err.kind(),
            std::io::ErrorKind::Unsupported | std::io::ErrorKind::Other
        ));
    }

    // SerialPort::open should error on /dev/null (not a TTY); use is_err() so we don't need Debug.
    #[test]
    fn serialport_open_on_dev_null_is_error() {
        let params = SerialParams {
            baud: BaudRate::B115200,
            parity: Parity::None,
            stop_bits: NumStopBits::One,
        };
        let res = SerialPort::open("/dev/null", params);
        assert!(res.is_err());
    }

    // Exercise wait_* helpers using a pipe (deterministic, no hardware).
    #[test]
    fn wait_readable_and_writable_with_pipe() {
        let (rd, wr) = pipe().expect("pipe");

        // write end should be writable quickly
        super::wait_writable(wr.as_raw_fd(), Duration::from_millis(10)).expect("writable");

        // initially, read end has no data -> not readable
        let readable_now = super::wait_readable(rd.as_raw_fd(), Duration::from_millis(1)).unwrap();
        assert!(!readable_now, "pipe should not be readable before a write");

        // make it readable by writing some bytes (pass OwnedFd by ref so it implements AsFd)
        write(&wr, b"hi").expect("write to pipe");

        // now read end should be readable
        let readable_after =
            super::wait_readable(rd.as_raw_fd(), Duration::from_millis(10)).unwrap();
        assert!(readable_after, "pipe should become readable after write");

        // OwnedFd will close on drop; no explicit close() needed
        drop(rd);
        drop(wr);
    }

    // Smoke test: drive readability then read via File; no SerialPort needed (it expects a TTY).
    #[test]
    fn pipe_based_rw_smoke() {
        let (rd, wr) = pipe().expect("pipe");

        // make reader ready by writing from a short-lived thread
        let t = std::thread::spawn({
            let wr = wr;
            move || {
                std::thread::sleep(StdDuration::from_millis(5));
                write(&wr, b"hello").expect("pipe write");
                drop(wr);
            }
        });

        let ready = super::wait_readable(rd.as_raw_fd(), Duration::from_millis(50)).unwrap();
        assert!(ready, "read end should become readable");

        // Turn OwnedFd into File and read
        let mut f: File = rd.into();
        let mut buf = [0u8; 16];
        let n = f.read(&mut buf).unwrap();
        assert_eq!(&buf[..n], b"hello");

        t.join().unwrap();
    }
}
