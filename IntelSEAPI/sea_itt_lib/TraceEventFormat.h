/*********************************************************************************************************************************************************************************************************************************************************************************************
#   Intel® Single Event API
#
#   This file is provided under the BSD 3-Clause license.
#   Copyright (c) 2015, Intel Corporation
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
#       Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
#       Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
#       Neither the name of the Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
#   IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
#   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
**********************************************************************************************************************************************************************************************************************************************************************************************/

#pragma once

#include "Utils.h"

#ifndef _WIN32
    #include <unistd.h>
    #include <sys/syscall.h>
#endif

#ifdef _WIN32
    static const int64_t g_PID = (int64_t)GetCurrentProcessId();
#else
    static const int64_t g_PID = (int64_t)getpid();
#endif

//https://github.com/google/trace-viewer
//For ETW see here: http://git.chromium.org/gitweb/?p=chromium/src.git;a=commitdiff;h=41fabf8e2dd3a847cbdad05da9b43fd9a99d741a (content/browser/tracing/etw_system_event_consumer_win.cc)
//parser source: https://github.com/google/trace-viewer/blob/49d0dd94c3925c3721d059ad3ee2db51d176248c/trace_viewer/extras/importer/trace_event_importer.html
//parser source: https://android.googlesource.com/platform/external/chromium-trace/+/6833e18b1d4077bf3a727b4422cc2acdbeee35a7/trace-viewer/src/tracing/importer/trace_event_importer.js
class CTraceEventFormat
{
public:
    struct SRegularFields
    {
        int64_t pid;
        int64_t tid;
        uint64_t nanoseconds;
    };

    enum EventPhase
    {
        Begin = 'B', //name, pid, tid, ts
        End = 'E', //name, pid, tid, ts
        Complete = 'X', //name, pid, tid, ts, dur
        Instant = 'i', //name, pid, tid, ts, s = (g, p, t) //vertical line
        Counter = 'C', //name, pid, tid, ts //"args": {"cats":  0, "dogs": 7}
        AsyncBegin = 'b', //name, pid, tid, ts, id
        AsyncInstant = 'n', //name, pid, tid, ts, id
        AsyncEnd = 'e', //name, pid, tid, ts, id
        //'S', 'T', 'F',
        //'s', 't', 'f', //Flow events, with arrows: cool but unclear
        FlowStart = 's',
        FlowInstant = 't',
        FlowFinish = 'f',
        Metadata = 'M',
        Sample = 'P', //pid, tid, ts
        ObjectNew = 'N', //name, pid, tid, ts, id but no args!
        ObjectDelete = 'D', //name, pid, tid, ts, id but no args!
        ObjectSnapshot = 'O', //name, pid, tid, ts, id, can have args! See snapshot.basetype for deeper.
    };

    static uint64_t GetTimeNS()
    {
#ifdef _WIN32
        return SHiResClock::now64(); //in nanoseconds
#else
        using namespace std::chrono;
        return (uint64_t)duration_cast<nanoseconds>(SHiResClock::now().time_since_epoch()).count();
#endif
    }

    static SRegularFields GetRegularFields()
    {
        return SRegularFields{
    #ifdef _WIN32
            g_PID, (int64_t)GetCurrentThreadId(),
    #elif defined(__ANDROID_API__)
            g_PID, (int64_t)gettid(),
    #elif __linux__
            g_PID, (int64_t)syscall(SYS_gettid),
    #else
            g_PID, (int64_t)syscall(SYS_thread_selfid),
#endif
            GetTimeNS()
        };
    }

    class CArgs
    {
    protected:
        typedef std::map<std::string, std::string> TMap;
        TMap m_args;
    public:
        CArgs(){}
        template<class T>
        CArgs(const std::string& name, const T& value)
        {
            Add(name, value);
        }
        CArgs& Add(const std::string& name, const char* value)
        {
            m_args[name] = value ? value : "";
            return *this;
        }
        template<class T>
        CArgs& Add(const std::string& name, const T& value)
        {
            m_args[name] = std::to_string(value);
            return *this;
        }
        operator bool() const {return !m_args.empty();}


        std::string Str() const
        {
            std::string res;
            for (const auto& pair: m_args)
            {
                if (!res.empty()) res += ";";
                res += pair.first + "=" + pair.second;
            }
            return res;
        }
        const TMap& GetMap() const {return m_args;}
    };
#ifdef __ANDROID_API__
    void WriteEvent(
        EventPhase ph,
        const std::string& name,
        const CArgs& args = CArgs(),
        const SRegularFields* pRegularFields = nullptr, //generated if omit
        const char* categories = nullptr,
        const uint64_t* pID = nullptr, //see EventPhase to understand when demanded
        const uint64_t* pDur = nullptr //for Complete events, same timestamp as in SRegularFields
    ) {
        int pFile = GetTraceFile();
        if (!pFile) return;
        char phase[2] = {(char)ph, 0};
        SRegularFields rf = pRegularFields ? *pRegularFields : GetRegularFields();
        std::ostringstream ss;
        switch(ph)
        {
            case Begin:
                ss << phase << "|" << rf.pid << "|" << name;
                ss << "|"; if (args) ss << args.Str(); // (arg1=val1;arg2=val2;...) << category
                ss << "|"; if (categories) ss << categories;
                break;
            case End:
                ss << phase; //can have arguments in third place
                if (args) ss << "||" << args.Str();
                break;
            case Counter:
                for (const auto& pair: args.GetMap())
                {
                    ss << phase << "|" << rf.pid << "|" << pair.first << "|" << pair.second << "|" << name;
                    break; //currently only one counter at a time is supported
                }
                break;
            //case AsyncBegin:
            case FlowStart:
                ss << "S|" << rf.pid << "|" << name << "|0x" << std::hex << (pID ? *pID : ~0x0);
                break;
            //case AsyncEnd:
            case FlowFinish:
                ss << "F|" << rf.pid << "|" << name << "|0x" << std::hex << (pID ? *pID : ~0x0);
                break;
            default:
                ss << phase;
                break;
        }
        ss << std::endl;
        std::string text = ss.str();
        write(pFile, text.c_str(), text.size());
    }
protected:
    static int GetTraceFile()
    {
        static thread_local int trace_marker = open("/sys/kernel/debug/tracing/trace_marker", O_WRONLY);
        return (trace_marker > 0) ? trace_marker : 0;
    }
#endif
};
