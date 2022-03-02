# HospitalsCovidTracker

HospitalsCovidTracker is a PeerToPeer command line application for the sharing of constantly updated data about new cases or swabs related to Covid19 pandemic.

The application is developed using C's sockets. 

[Here](specifiche.pdf) (in Italian) are present the requirements and the application domain.

The implemented protocol to develop the application is detailed [Here](documentazione.pdf) (in Italian) with the entries' format saved in the registries and exchanged from the application.

## Goals
- Create a communication protocol to allows peers of a ring (P2P) network to comunicate
- Structure entries' format exchanged between peers and saved on daily registries
- Create a C application that uses sockets 

## Project structure
 - `ds.c`: discovery server
 - `peer.c`: peer
 - `registries_examples`: contains some examples of peer's daily registries
 - `exec.sh`: bash script that launches the discovery server on port 4242 and 5 peers on ports 5001-5005
 - `Makefile`: script to launch the 'make' command (in the 'exec.sh' script) to compile files of the project
