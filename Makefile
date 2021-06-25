#makefile for TTFTP program                                                
CCC=g++
CXXLAGS=-Wall -g  -pedantic
CXXLINK=$(CCC)
OBJS= main.o 
RM=rm -f

#creating executable filter                      
ttftps: $(OBJS)
	$(CXXLINK) -o ttftps  $(OBJS) #-lp 
 
#create object files
main.o: main.cpp 

#cleaning old files before new make                                
clean:
	$(RM) ttftps *.o *.bak *~ "#"* log.txt
