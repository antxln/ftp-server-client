# ftp-server-client

## Description
This project is for Data Comm. & Networking course, featuring concurrent File Transfer Protocol server and associated client
program. The server and client support login/logout and getting and sending files (ASCII & Binary). Login details are username _user_ and password _pass_, hardcoded in source code, because user authentication was not in the scope of the project. Shell files were scripts provided to students for testing.

## Build
### Server

  Build
  ```
  gcc server.c -pthread -o server
  ```
  Run
  ```
  ./server 127.0.0.1
  ```
### Client
  Build
  ```
  gcc client.c -o client
  ```
  Run
  ```
  ./client
  ```

## Message Protocol
![image](https://user-images.githubusercontent.com/46301114/200375836-78cf575d-75ec-42af-b307-fa5a88b416ca.png)
