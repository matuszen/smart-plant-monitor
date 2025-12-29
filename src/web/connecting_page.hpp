#pragma once

#include <string_view>

inline constexpr std::string_view CONNECTING_PAGE_HTML = R"HTML(
    <!DOCTYPE html>
    <html lang="en">

    <head>
        <meta charset="utf-8">
        <title>Connecting</title>
    </head>

    <body>
        <h3>Using stored credentials...</h3>
        <p>AP will stop and the device will connect now.</p>
    </body>

    </html>
    )HTML";
