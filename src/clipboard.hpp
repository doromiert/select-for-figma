#pragma once
#include "include/cef_render_process_handler.h"
class Browser;
namespace Clipboard {
CefRefPtr<CefRenderProcessHandler> renderer_handler();
bool receive(Browser& browser, CefRefPtr<CefFrame> frame, CefProcessId source, CefRefPtr<CefProcessMessage> message);
void check_copy(Browser& browser);
void copy(Browser& browser, bool cut = false);
void paste(Browser& browser, bool plain_text = false);
}
