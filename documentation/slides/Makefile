outputfolder ?= ./pdf
slides: compile-slides.sh
	mkdir -p $(outputfolder)
	sh compile-slides.sh $(outputfolder)
clean:
	rm -rf $(outputfolder)
