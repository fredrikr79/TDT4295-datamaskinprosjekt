OUTPUT_FOLDER := pdf

TYPST_FILES := $(shell find . -maxdepth 1 -type f -name '*.typ')
PDF_SLIDES := $(addprefix $(OUTPUT_FOLDER)/,$(TYPST_FILES:.typ=.pdf))

.PHONY: slides clean

slides: $(PDF_SLIDES)
	echo $<
$(OUTPUT_FOLDER)/%.pdf: %.typ | $(OUTPUT_FOLDER)
	typst compile $< $@
$(OUTPUT_FOLDER):
	mkdir -p $@
clean:
	rm -rf $(OUTPUT_FOLDER)
