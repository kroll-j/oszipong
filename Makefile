all:	oszipong

run:	oszipong
	-./oszipong

oszipong:	oszipong.cc
		g++ -O2 oszipong.cc -ooszipong 
