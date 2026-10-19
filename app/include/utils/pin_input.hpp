#pragma once

#include <functional>
#include <string>

// Asks for a numeric PIN (4-8 digits) and hands it back as a string.
//
// borealis' own openForNumber() isn't usable for this: it returns a `long`,
// so a PIN like "0123" would come back as 123. On the Switch this opens the
// system keyboard directly instead -- numeric pad, digits hidden as they're
// typed. On PC it's the regular text input dialog.
//
// `onDone` only runs if something was entered (not on cancel).
namespace pin_input
{

void open(const std::string& header, const std::string& subText, std::function<void(std::string)> onDone);

} // namespace pin_input
