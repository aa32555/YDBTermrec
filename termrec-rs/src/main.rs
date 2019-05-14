extern crate libc;
extern crate clap;
#[macro_use]
extern crate yottadb;
#[macro_use]
extern crate nom;

use std::error::Error;
use std::time::{SystemTime, UNIX_EPOCH};
use std::io::{self};
use std::io::prelude::*;
use std::num::ParseIntError;
use std::thread;
use std::time;

use clap::{Arg, App, AppSettings, SubCommand, ArgMatches};
use yottadb::context_api::Context;

/*
 * To record output from another program, we can either fix its 0 FD before starting it,
 * or record system writes at runtime using something like strace.
 * Using strace has a problem in that the amount it will output is a fixed size, 
 * which means there will eventually be a string large enough we can't capture it.
 * Spawning a new process after dup2 isn't great either because we can't attach to
 * an existing process
 *
 * We could use GDB to attach to a process and dup2 something... That's a thought.
 * For now, just assume we'll only monitor processes that we start
 *
 * The magic command: sudo strace -f -x -s 4096 -e abbrev=none -e trace=write -e write=0 -v -o '|cat' -p 13107
 * The magic command: sudo strace -f -x -s 8192 -e abbrev=none -e trace=write -e write=1,2 -v -p 13107
 *strace -x -f -s 8192 -e trace=write -e write=1,2 -p 30149
 */

#[derive(Debug, PartialEq)]
pub struct SyscallRecord {
    pub function: String,
    pub fd: usize,
    pub val: Vec<u8>,
}

fn is_char(c: u8) -> bool {
    c.is_ascii_alphabetic()
}

fn replace_hex_chars(v: Vec<u8>) -> Vec<u8> {
    let mut i = 0;
    let mut out = Vec::with_capacity(v.len());
    while i < v.len() {
        match v[i] {
            b'\\' => {
                match v[i+1] {
                    b'x' => {
                        // The only remaining backslashes are escaped hex codes
                        let hex_sequence = [v[i+2], v[i+3]];
                        i += 3;
                        let val = u8::from_str_radix(&String::from_utf8_lossy(&hex_sequence), 16).unwrap();
                        out.push(val)
                    }
                    _ => out.push(v[i+1])
                }
            }
            _ => out.push(v[i]),
        }
        i += 1;
    }
    out
}
named!(escaped_string <Vec<u8>>, map!(escaped_transform!(take_until_either1!("\"\\"), '\\', alt!(
                  tag!("\\") => { |_| &b"\\"[..] }
                | tag!("\"") => { |_| &b"\""[..] }
                | tag!("n") => { |_| &[10][..] }
                | tag!("x") => { |_| &b"\\x"[..] }
            )), replace_hex_chars));

named!(function_name, do_parse!(val: take_while!( is_char ) >> (val)));
named!(read_usize<&[u8], Result<usize, ParseIntError>>,
       map!(nom::digit, |val: &[u8]| String::from_utf8_lossy(val).parse::<usize>()));

named!(syscall_record<&[u8], SyscallRecord>,
       ws!(do_parse!(
               opt!(tag!("[pid")) >> opt!(read_usize) >> opt!(tag!("]")) >>
               function: function_name >>
               tag!("(") >> fd: read_usize >> tag!(",") >> tag!("\"") >>
               val: escaped_string >> tag!("\"") >>
               (SyscallRecord{function: String::from_utf8_lossy(function).into_owned(),
                    fd: fd.unwrap(), val: Vec::from(val)})
           )
      ));

fn record(val: &[u8], session: &str, ctx: &Context, start_time: &time::Instant) -> Result<(), Box<Error>> {
    let time = start_time.elapsed().as_millis();
    let mut k = make_ckey!(ctx, "^termrec", session, time.to_string());
    let unique_id = k.increment(None)?;
    k.push(unique_id);
    k.set(&Vec::from(val))?;
    Ok(())
}

fn handle_record(ctx: &Context, _matches: &ArgMatches) -> Result<(), Box<Error>> {
    let mut session_key = make_ckey!(ctx, "^termrec");
    session_key.increment(None)?;
    let session = session_key.get()?;
    let session = String::from_utf8_lossy(&session);
    let start_time = time::Instant::now();
    println!("Recording session {}", session);
    let stdin = io::stdin();
    for line in stdin.lock().lines() {
        let rec = line?; let rec = rec.as_bytes(); let rec = syscall_record(&rec);
        if rec.is_err() { continue; }
        let rec = rec.unwrap().1;
        record(&rec.val, &session, &ctx, &start_time)?;
    }
    println!("Done!");
    Ok(())
}

fn handle_sleep(cur_time: &[u8], prev_time: u64) -> Result<u64, Box<Error>> {
    let pause_time = String::from_utf8_lossy(cur_time);
    let pause_time = pause_time.parse::<u64>()?;
    if prev_time != 0 {
        let sleep = time::Duration::from_millis((pause_time - prev_time) as u64);
        thread::sleep(sleep);
    }
    Ok(pause_time)
}

fn handle_playback(ctx: &Context, matches: &ArgMatches) -> Result<(), Box<Error>> {
    let session_id = matches.value_of("session_id").unwrap();
    println!("Playing back session {}", session_id);
    let mut session_key = make_ckey!(ctx, "^termrec", session_id, "0");
    let mut start_time = 0;
    let stdout = io::stdout();
    for v in session_key.iter_key_subs() {
        let mut k = v?;
        // Pause if we need too
        start_time = handle_sleep(&k[2], start_time)?;
        k.push(Vec::from(""));
        for v in k.iter_values() {
            let val = v?;
            stdout.lock().write(&val)?;
            stdout.lock().flush()?;
        }
    }
    Ok(())
}

fn handle_list(ctx: &Context, matches: &ArgMatches) -> Result<(), Box<Error>> {
    println!("Listing sessions");
    let mut sessions_key = make_ckey!(ctx, "^termrec", "0");
    let stdout = io::stdout();
    for v in sessions_key.iter_key_subs() {
        let k = v?;
        let session = String::from_utf8_lossy(&k[1]);
        println!("Session: {}", session);
    }
    Ok(())
}

fn main() -> Result<(), Box<Error>> {
    let matches = App::new("termrec")
        .version("1.0")
        .author("Charles Hathaway <charles@yottadb.com>")
        .about("Attaches to an interactive shell and records the terminal output")
        .subcommand(SubCommand::with_name("list")
                    .about("Lists record session ID's"))
        .subcommand(SubCommand::with_name("record")
                    .about("Records a session"))
        .subcommand(SubCommand::with_name("play")
                    .about("Play a session")
                    .arg(Arg::with_name("session_id").help("Session to replay").required(true)
                         .index(1)))
        .setting(AppSettings::ArgRequiredElseHelp)
        .get_matches();

    let ctx = Context::new();
    if let Some(matches) = matches.subcommand_matches("record") {
        handle_record(&ctx, matches)?;
    }
    if let Some(matches) = matches.subcommand_matches("play") {
        handle_playback(&ctx, matches)?;
    }
    if let Some(matches) = matches.subcommand_matches("list") {
        handle_list(&ctx, matches)?;
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_read_string() {
        let r = function_name(&b"read("[..]);
        assert_eq!(r, Ok((&b"("[..], &b"read"[..])));
    }

    #[test]
    fn test_escaped_string() {
        let r = escaped_string(&b"hello world!\""[..]);
        assert_eq!(r, Ok((&b"\""[..], &b"hello world!"[..])));
    }

    #[test]
    fn test_escaped_string2() {
        let r = escaped_string(&b"\\33[?1h\\33=\\\"\""[..]);
        assert_eq!(r, Ok((&b"\""[..], &b"\\33[?1h\\33=\\\""[..])));
    }

    #[test]
    fn test_escaped_string3() {
        let r = escaped_string(&b"hello world!\\\""[..]);
        assert_eq!(r, Ok((&b""[..], &b"hello world!\\\""[..])));
    }

    #[test]
    fn test_escaped_string4() {
        let r = escaped_string(&b"hello world!\\\"\""[..]);
        assert_eq!(r, Ok((&b"\""[..], &b"hello world!\\\""[..])));
    }

    #[test]
    fn test_escaped_string5() {
        let r = escaped_string(&b"hello world!\\33\""[..]);
        assert_eq!(r, Ok((&b"\""[..], &b"hello world!\\33"[..])));
    }

    #[test]
    fn test_read_syscall_record() {
        let r = syscall_record(&b"write(10, \"\\33[?2004h\", 8)"[..]);
        assert_eq!(r, Ok((&b", 8)"[..], SyscallRecord{function: String::from("write"), fd: 10, val: Vec::from("\\33[?2004h")})))
    }

    #[test]
    fn test_read_syscall_record2() {
        let r = syscall_record(&b"[pid 17025] write(10, \"\\33[?2004h\", 8)"[..]);
        assert_eq!(r, Ok((&b", 8)"[..], SyscallRecord{function: String::from("write"), fd: 10, val: Vec::from("\\33[?2004h")})))
    }

}
