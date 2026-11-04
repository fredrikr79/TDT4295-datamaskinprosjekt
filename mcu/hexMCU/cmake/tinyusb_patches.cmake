# Small fixes to TinyUSB's dwc2 host driver, applied to a copy in the build
# directory, so third_party/tinyusb itself stays untouched and every team
# member gets them just by building.
#
#   hexmcu_tinyusb_hcd_dwc2(<out_var>)
#
# sets <out_var> to the patched hcd_dwc2.c, to compile instead of the one in
# third_party (add HEXMCU_HCD_DWC2_INCLUDE to the include directories too).
# If a TinyUSB update changes a patched spot, configuring stops with an error
# instead of silently building the unpatched driver.
#
# Both fixes are for low/full-speed devices (keyboard, mouse) behind a
# high-speed hub. Each poll is split in two: a start split tells the hub's
# transaction translator (TT) to fetch the report on the slow bus, then
# complete splits collect it. The hub answers NYET ("not yet") while that's
# still going on. On the Nucleo, with a keyboard and a mouse on one hub,
# TinyUSB gave up more polls than it got reports, and a device could lose
# every report until replugged. Each given-up poll loses its report: the
# device already handed it to the hub and won't send it again.
#
# 1. NYET limit (HCD_XFER_PERIOD_SPLIT_NYET_MAX): TinyUSB collects 3 times,
#    then gives up. 7 (the most its 3-bit counter holds) keeps collecting
#    for the rest of the frame, as Linux's dwc2 driver does.
#
# 2. Start splits early in the frame (HEXMCU_SPLIT_LAST_UFRAME). The hub runs
#    the slow transactions inside its own 1 ms frame and won't start one too
#    close to the frame's end. A start split sent late in a frame (microframe
#    6, 7, ...) is never run, so every complete split gets NYET. TinyUSB
#    keeps each endpoint on a fixed microframe, so a device stuck in such a
#    slot loses every report until a replug moves it. Here a periodic split
#    transfer whose start split would go out after microframe
#    HEXMCU_SPLIT_LAST_UFRAME is held back to microframe 0 of the next frame,
#    which then becomes its fixed slot. (USB 2.0 11.18; EHCI hosts schedule
#    these start splits early in the frame too.)
#
# -DHEXMCU_TINYUSB_PATCHES=OFF builds the original driver, to compare.

option(HEXMCU_TINYUSB_PATCHES "Patch TinyUSB's dwc2 host driver (see tinyusb_patches.cmake)" ON)
set(HEXMCU_SPLIT_NYET_MAX    7)
set(HEXMCU_SPLIT_LAST_UFRAME 3)

# Replace <old> with <new> in the variable <text_var>; stop if <old> isn't there.
function(_hexmcu_patch text_var old new what)
    string(FIND "${${text_var}}" "${old}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR
            "tinyusb_patches.cmake: can't apply '${what}', TinyUSB's hcd_dwc2.c changed.\n"
            "Expected to find:\n${old}\n"
            "Check whether TinyUSB fixed it itself, then update or drop the patch.")
    endif()
    string(REPLACE "${old}" "${new}" out "${${text_var}}")
    set(${text_var} "${out}" PARENT_SCOPE)
endfunction()

function(hexmcu_tinyusb_hcd_dwc2 out_var)
    set(src ${TINYUSB_ROOT}/src/portable/synopsys/dwc2/hcd_dwc2.c)
    if(NOT HEXMCU_TINYUSB_PATCHES)
        message(STATUS "hexMCU: TinyUSB dwc2 patches OFF")
        set(${out_var} ${src} PARENT_SCOPE)
        return()
    endif()
    set(dst ${CMAKE_CURRENT_BINARY_DIR}/tinyusb_patched/hcd_dwc2.c)
    file(READ ${src} text)

    _hexmcu_patch(text
        "HCD_XFER_PERIOD_SPLIT_NYET_MAX = 3,"
        "HCD_XFER_PERIOD_SPLIT_NYET_MAX = ${HEXMCU_SPLIT_NYET_MAX}, // hexMCU: was 3, see cmake/tinyusb_patches.cmake"
        "NYET limit")

    _hexmcu_patch(text
"static bool edpt_xfer_kickoff(dwc2_regs_t* dwc2, uint8_t ep_id) {
  uint8_t ch_id = channel_alloc(dwc2);"
"static bool edpt_xfer_kickoff(dwc2_regs_t* dwc2, uint8_t ep_id) {
  // hexMCU: start periodic splits early in the frame, see cmake/tinyusb_patches.cmake
  {
    hcd_endpoint_t* const sp_edpt = &_hcd_data.edpt[ep_id];
    if (channel_is_periodic(sp_edpt->hcchar) && sp_edpt->hcsplt_bm.split_en) {
      const uint32_t sp_uframe = (dwc2->hfnum + 1u) & 7u; // the start split goes out next microframe
      if (sp_uframe > ${HEXMCU_SPLIT_LAST_UFRAME}u) {
        periodic_xfer_defer(dwc2, sp_edpt, 8u - sp_uframe); // SOF kicks it off again before microframe 0
        return true;
      }
    }
  }
  uint8_t ch_id = channel_alloc(dwc2);"
        "start splits early in the frame")

    # Rewrite only when the content changes, so it doesn't rebuild every
    # configure, and configure again when TinyUSB's file changes.
    file(WRITE ${dst}.tmp "${text}")
    configure_file(${dst}.tmp ${dst} COPYONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${src})

    set(${out_var} ${dst} PARENT_SCOPE)
endfunction()

# The copy includes "dwc2_common.h" etc. relative to its original folder.
set(HEXMCU_HCD_DWC2_INCLUDE ${TINYUSB_ROOT}/src/portable/synopsys/dwc2)