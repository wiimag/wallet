// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.


#ifndef IMGUIDATECHOOSER_H_
#define IMGUIDATECHOOSER_H_

#ifndef IMGUI_API
#include <imgui.h>
#endif //IMGUI_API

struct tm;  // defined in <time.h>

namespace ImGui {

/*! Date picker widget.
 *
 *  @param label Label to display before the widget.
 *  @param dateOut Date to display and modify.
 *  @param dateFormat Format string to use when displaying the date.
 *  @param closeWhenMouseLeavesIt If true, the widget will close when the mouse leaves it.
 *  @param pSetStartDateToDateOutThisFrame If not NULL, the widget will set the start date to the dateOut value this frame.
 *  @param leftArrow If not NULL, the widget will display this string as the left arrow.
 *  @param rightArrow If not NULL, the widget will display this string as the right arrow.
 *  @param upArrowString If not NULL, the widget will display this string as the up arrow.
 *  @param downArrowString If not NULL, the widget will display this string as the down arrow.
 *
 *  @return True if the user has selected a date, false otherwise.
 */
IMGUI_API bool DateChooser(const char* label,tm& dateOut,const char* dateFormat="%d/%m/%Y",
    bool closeWhenMouseLeavesIt=true,bool* pSetStartDateToDateOutThisFrame=NULL,
    const char* leftArrow=nullptr,const char* rightArrow=nullptr,
    const char* upArrowString="   ^   ",const char* downArrowString="   v   ");

/*! Reset the date to zero.
 *
 *  @param date Date to reset.
 */
IMGUI_API void SetDateZero(tm* date);

/*! Set the date to today.
 *
 *  @param date Date to set.
 */
IMGUI_API void SetDateToday(tm* date);

} // namespace ImGui



#endif
