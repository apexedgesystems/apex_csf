pub mod io;
pub mod probe; // SerialProbe & types // SerialPort for active read/write

pub use io::SerialPort;
pub use probe::{
    BaudRate, NumStopBits, Parity, ProbeOptions, ProbeReport, ProbeStatus, SerialParams,
    SerialProbe,
};
