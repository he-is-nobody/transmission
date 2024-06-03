#include "Application.h"
#include "InteropHelper.h"
#include "Prefs.h"

#include <QtDebug>

#include <array>
#include <memory>
#include <string_view>

#include <libtransmission/transmission.h>

#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

using namespace std::string_view_literals;

namespace
{

char const* const DisplayName = "transmission-qt";

std::array<tr_option, 8> const Opts = {
    tr_option{ 'g', "config-dir", "Where to look for configuration files", "g", true, "<path>" },
    { 'm', "minimized", "Start minimized in system tray", "m", false, nullptr },
    { 'p', "port", "Port to use when connecting to an existing session", "p", true, "<port>" },
    { 'r', "remote", "Connect to an existing session at the specified hostname", "r", true, "<host>" },
    { 'u', "username", "Username to use when connecting to an existing session", "u", true, "<username>" },
    { 'v', "version", "Show version number and exit", "v", false, nullptr },
    { 'w', "password", "Password to use when connecting to an existing session", "w", true, "<password>" },
    { 0, nullptr, nullptr, nullptr, false, nullptr }
};

char const* getUsage()
{
    return "Usage:\n"
           "  transmission-qt [options...] [torrent files] [-- Qt options]";
}

} // namespace

int tr_main(int argc, char** argv)
{
    auto const init_mgr = tr_lib_init();

    tr_locale_set_global("");

    // parse the command-line arguments
    int c = 0;
    bool minimized = false;
    char const* optarg = nullptr;
    QString host;
    QString port;
    QString username;
    QString password;
    QString config_dir;
    QStringList filenames;
    int qt_args_start_idx = -1;

    while (qt_args_start_idx < 0 &&
           (c = tr_getopt(getUsage(), argc, const_cast<char const**>(argv), Opts.data(), &optarg)) != TR_OPT_DONE)
    {
        switch (c)
        {
        case 'g':
            config_dir = QString::fromUtf8(optarg);
            break;

        case 'p':
            port = QString::fromUtf8(optarg);
            break;

        case 'r':
            host = QString::fromUtf8(optarg);
            break;

        case 'u':
            username = QString::fromUtf8(optarg);
            break;

        case 'w':
            password = QString::fromUtf8(optarg);
            break;

        case 'm':
            minimized = true;
            break;

        case 'v':
            qInfo() << DisplayName << LONG_VERSION_STRING;
            return 0;

        case TR_OPT_ERR:
            qWarning() << "Invalid option";
            tr_getopt_usage(DisplayName, getUsage(), Opts.data());
            return 1;

        default:
            if (optarg == "--"sv)
            {
                qt_args_start_idx = tr_optind;
                break;
            }

            filenames.append(QString::fromUtf8(optarg));
            break;
        }
    }

    // set the fallback config dir
    if (config_dir.isNull())
    {
        config_dir = QString::fromStdString(tr_getDefaultConfigDir("transmission"));
    }

    {
        // try to delegate the work to an existing copy of Transmission
        // before starting ourselves...
        InteropHelper::initialize();
        InteropHelper const interop_client;

        if (interop_client.isConnected())
        {
            bool delegated = false;

            for (QString const& filename : filenames)
            {
                auto const a = AddData(filename);
                QString metainfo;

                switch (a.type)
                {
                case AddData::URL:
                    metainfo = a.url.toString();
                    break;

                case AddData::MAGNET:
                    metainfo = a.magnet;
                    break;

                case AddData::FILENAME:
                case AddData::METAINFO:
                    metainfo = QString::fromUtf8(a.toBase64());
                    break;

                default:
                    break;
                }

                if (!metainfo.isEmpty() && interop_client.addMetainfo(metainfo))
                {
                    delegated = true;
                }
            }

            if (delegated)
            {
                return 0;
            }
        }
    }

    // initialize the prefs
    auto prefs = std::make_unique<Prefs>(config_dir);

    if (!host.isNull())
    {
        prefs->set(Prefs::SESSION_REMOTE_HOST, host);
    }

    if (!port.isNull())
    {
        prefs->set(Prefs::SESSION_REMOTE_PORT, port.toUInt());
    }

    if (!username.isNull())
    {
        prefs->set(Prefs::SESSION_REMOTE_USERNAME, username);
    }

    if (!password.isNull())
    {
        prefs->set(Prefs::SESSION_REMOTE_PASSWORD, password);
    }

    if (!host.isNull() || !port.isNull() || !username.isNull() || !password.isNull())
    {
        prefs->set(Prefs::SESSION_IS_REMOTE, true);
    }

    if (prefs->getBool(Prefs::START_MINIMIZED))
    {
        minimized = true;
    }

    // start as minimized only if the system tray present
    if (!prefs->getBool(Prefs::SHOW_TRAY_ICON))
    {
        minimized = false;
    }

    auto qt_argv = std::vector<char*>{ argv[0] };
    auto qt_argc = 1;

    if (qt_args_start_idx >= 0)
    {
        qt_argv.insert(qt_argv.end(), &argv[qt_args_start_idx], &argv[argc]);
        qt_argc = qt_argv.size();
    }

    Application const app(std::move(prefs), minimized, config_dir, filenames, qt_argc, qt_argv.data());
    return QApplication::exec();
}
