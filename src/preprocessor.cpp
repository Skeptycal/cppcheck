/*
 * cppcheck - c/c++ syntax checking
 * Copyright (C) 2007-2009 Daniel Marjamäki, Reijo Tomperi, Nicolas Le Cam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/
 */


#include "preprocessor.h"
#include "tokenize.h"
#include "token.h"

#include <algorithm>

#include <sstream>

#ifdef __BORLANDC__
#include <ctype>
#endif


Preprocessor::Preprocessor()
{

}

/** Just read the code into a string. Perform simple cleanup of the code */
std::string Preprocessor::read(std::istream &istr)
{
    // Get filedata from stream..
    bool ignoreSpace = true;

    // For the error report
    int lineno = 1;

    std::ostringstream code;
    for (char ch = (char)istr.get(); istr.good(); ch = (char)istr.get())
    {
        if (ch < 0)
            continue;

        if (ch == '\n')
            ++lineno;

        // Replace assorted special chars with spaces..
        if ((ch != '\n') && (isspace(ch) || iscntrl(ch)))
            ch = ' ';

        // Skip spaces after ' ' and after '#'
        if (ch == ' ' && ignoreSpace)
            continue;
        ignoreSpace = bool(ch == ' ' || ch == '#' || ch == '/');

        // Remove comments..
        if (ch == '/')
        {
            char chNext = (char)istr.get();

            if (chNext == '/')
            {
                while (istr.good() && ch != '\n')
                    ch = (char)istr.get();
                code << "\n";
                ++lineno;
            }

            else if (chNext == '*')
            {
                char chPrev = 0;
                while (istr.good() && (chPrev != '*' || ch != '/'))
                {
                    chPrev = ch;
                    ch = (char)istr.get();
                    if (ch == '\n')
                    {
                        code << "\n";
                        ++lineno;
                    }
                }
            }

            else
            {
                if (chNext == '\n')
                    ++lineno;
                code << std::string(1, ch) << std::string(1, chNext);
            }
        }

        // String constants..
        else if (ch == '\"')
        {
            code << "\"";
            do
            {
                ch = (char)istr.get();
                code << std::string(1, ch);
                if (ch == '\\')
                {
                    ch = (char)istr.get();
                    code << std::string(1, ch);

                    // Avoid exiting loop if string contains "-characters
                    ch = 0;
                }
            }
            while (istr.good() && ch != '\"');
        }

        // char constants..
        else if (ch == '\'')
        {
            code << "\'";
            ch = (char)istr.get();
            code << std::string(1, ch);
            if (ch == '\\')
            {
                ch = (char)istr.get();
                code << std::string(1, ch);
            }
            ch = (char)istr.get();
            code << "\'";
        }

        // Just some code..
        else
        {
            code << std::string(1, ch);
        }
    }

    return code.str();
}

void Preprocessor::preprocess(std::istream &istr, std::map<std::string, std::string> &result)
{
    std::list<std::string> configs;
    std::string data;
    preprocess(istr, data, configs);
    for (std::list<std::string>::const_iterator it = configs.begin(); it != configs.end(); ++it)
        result[ *it ] = Preprocessor::getcode(data, *it);
}

std::string Preprocessor::removeSpaceNearNL(const std::string &str)
{
    std::string tmp;
    int prev = -1;
    for (unsigned int i = 0; i < str.size(); i++)
    {
        if (str[i] == ' ' &&
            ((i > 0 && tmp[prev] == '\n') ||
             (i + 1 < str.size() && str[i+1] == '\n')
            )
           )
        {
            // Ignore space that has new line in either side of it
        }
        else
        {
            tmp.append(1, str[i]);
            ++prev;
        }
    }

    return tmp;
}

std::string Preprocessor::replaceIfDefined(const std::string &str)
{
    std::string ret(str);
    std::string::size_type pos = 0;
    while ((pos = ret.find("#if defined(", pos)) != std::string::npos)
    {
        std::string::size_type pos2 = ret.find(")", pos + 9);
        if (pos2 > ret.length() - 1)
            break;
        if (ret[pos2+1] == '\n')
        {
            ret.erase(pos2, 1);
            ret.erase(pos + 3, 9);
            ret.insert(pos + 3, "def ");
        }
        ++pos;
    }

    return ret;
}

void Preprocessor::preprocess(std::istream &istr, std::string &processedFile, std::list<std::string> &resultConfigurations)
{
    processedFile = read(istr);

    // Replace all tabs with spaces..
    std::replace(processedFile.begin(), processedFile.end(), '\t', ' ');

    // Remove all indentation..
    if (!processedFile.empty() && processedFile[0] == ' ')
        processedFile.erase(0, processedFile.find_first_not_of(" "));

    // Remove space characters that are after or before new line character
    processedFile = removeSpaceNearNL(processedFile);

    // Using the backslash at the end of a line..
    std::string::size_type loc = 0;
    while ((loc = processedFile.rfind("\\\n")) != std::string::npos)
    {
        processedFile.erase(loc, 2);
        if (loc > 0 && processedFile[loc-1] != ' ')
            processedFile.insert(loc, " ");
        if ((loc = processedFile.find("\n", loc)) != std::string::npos)
            processedFile.insert(loc, "\n");
    }

    processedFile = replaceIfDefined(processedFile);

    processedFile = expandMacros(processedFile);

    // Get all possible configurations..
    resultConfigurations = getcfgs(processedFile);
}



// Get the DEF in this line: "#ifdef DEF"
std::string Preprocessor::getdef(std::string line, bool def)
{
    // If def is true, the line must start with "#ifdef"
    if (def && line.find("#ifdef ") != 0 && line.find("#if ") != 0 && line.find("#elif ") != 0)
    {
        return "";
    }

    // If def is false, the line must start with "#ifndef"
    if (!def && line.find("#ifndef ") != 0)
    {
        return "";
    }

    // Remove the "#ifdef" or "#ifndef"
    line.erase(0, line.find(" "));

    // Remove all spaces.
    while (line.find(" ") != std::string::npos)
        line.erase(line.find(" "), 1);

    // The remaining string is our result.
    return line;
}



std::list<std::string> Preprocessor::getcfgs(const std::string &filedata)
{
    std::list<std::string> ret;
    ret.push_back("");

    std::list<std::string> deflist;

    std::istringstream istr(filedata);
    std::string line;
    while (getline(istr, line))
    {
        std::string def = getdef(line, true) + getdef(line, false);
        if (!def.empty())
        {
            if (! deflist.empty() && line.find("#elif ") == 0)
                deflist.pop_back();
            deflist.push_back(def);
            def = "";
            for (std::list<std::string>::const_iterator it = deflist.begin(); it != deflist.end(); ++it)
            {
                if (*it == "0")
                    break;
                if (*it == "1")
                    continue;
                if (! def.empty())
                    def += ";";
                def += *it;
            }

            if (std::find(ret.begin(), ret.end(), def) == ret.end())
                ret.push_back(def);
        }

        if (line.find("#else") == 0 && ! deflist.empty())
        {
            std::string def((deflist.back() == "1") ? "0" : "1");
            deflist.pop_back();
            deflist.push_back(def);
        }

        if (line.find("#endif") == 0 && ! deflist.empty())
            deflist.pop_back();
    }

    return ret;
}



bool Preprocessor::match_cfg_def(std::string cfg, const std::string &def)
{
    if (def == "0")
        return false;

    if (def == "1")
        return true;

    if (cfg.empty())
        return false;

    while (! cfg.empty())
    {
        if (cfg.find(";") == std::string::npos)
            return bool(cfg == def);
        std::string _cfg = cfg.substr(0, cfg.find(";"));
        if (_cfg == def)
            return true;
        cfg.erase(0, cfg.find(";") + 1);
    }

    return false;
}


std::string Preprocessor::getcode(const std::string &filedata, std::string cfg)
{
    std::ostringstream ret;

    bool match = true;
    std::list<bool> matching_ifdef;
    std::list<bool> matched_ifdef;

    std::istringstream istr(filedata);
    std::string line;
    while (getline(istr, line))
    {
        std::string def = getdef(line, true);
        std::string ndef = getdef(line, false);

        if (line.find("#elif ") == 0)
        {
            if (matched_ifdef.back())
            {
                matching_ifdef.back() = false;
            }
            else
            {
                if (match_cfg_def(cfg, def))
                {
                    matching_ifdef.back() = true;
                    matched_ifdef.back() = true;
                }
            }
        }

        else if (! def.empty())
        {
            matching_ifdef.push_back(match_cfg_def(cfg, def));
            matched_ifdef.push_back(matching_ifdef.back());
        }

        else if (! ndef.empty())
        {
            matching_ifdef.push_back(! match_cfg_def(cfg, ndef));
            matched_ifdef.push_back(matching_ifdef.back());
        }

        else if (line == "#else")
        {
            if (! matched_ifdef.empty())
                matching_ifdef.back() = ! matched_ifdef.back();
        }

        else if (line == "#endif")
        {
            if (! matched_ifdef.empty())
                matched_ifdef.pop_back();
            if (! matching_ifdef.empty())
                matching_ifdef.pop_back();
        }

        if (!line.empty() && line[0] == '#')
        {
            match = true;
            for (std::list<bool>::const_iterator it = matching_ifdef.begin(); it != matching_ifdef.end(); ++it)
                match &= bool(*it);
        }
        if (! match)
            line = "";

        if (line.find("#if") == 0 ||
            line.find("#else") == 0 ||
            line.find("#elif") == 0 ||
            line.find("#endif") == 0)
            line = "";

        ret << line << "\n";
    }

    return ret.str();
}



std::string Preprocessor::expandMacros(std::string code)
{
    // Search for macros and expand them..
    std::string::size_type defpos = 0;
    while ((defpos = code.find("#define", defpos)) != std::string::npos)
    {
        // Get macro..
        std::string::size_type endpos = code.find("\n", defpos + 6);
        while (endpos != std::string::npos && code[endpos-1] == '\\')
            endpos = code.find("\n", endpos + 1);
        if (endpos == std::string::npos)
        {
            code.erase(defpos);
            break;
        }

        // Extract the whole macro into a separate variable "macro" and then erase it from "code"
        std::string macro(code.substr(defpos + 8, endpos - defpos - 7));
        code.erase(defpos, endpos - defpos);

        // Remove "\\\n" from the macro
        while (macro.find("\\\n") != std::string::npos)
        {
            macro.erase(macro.find("\\\n"), 2);
            code.insert(defpos, "\n");
            ++defpos;
        }

        // Tokenize the macro to make it easier to handle
        Tokenizer tokenizer;
        std::istringstream istr(macro.c_str());
        tokenizer.tokenize(istr, "");

        // Extract macro parameters
        std::vector<std::string> macroparams;
        if (Token::Match(tokenizer.tokens(), "%var% ( %var%"))
        {
            for (const Token *tok = tokenizer.tokens()->tokAt(2); tok; tok = tok->next())
            {
                if (tok->str() == ")")
                    break;
                if (tok->isName())
                    macroparams.push_back(tok->str());
            }
        }

        // Expand all macros in the code..
        const std::string macroname(tokenizer.tokens()->str());
        std::string::size_type pos1 = defpos;
        while ((pos1 = code.find(macroname, pos1 + 1)) != std::string::npos)
        {
            // Previous char must not be alphanumeric or '_'
            if (pos1 != 0 && (std::isalnum(code[pos1-1]) || code[pos1-1] == '_'))
                continue;

            std::vector<std::string> params;
            std::string::size_type pos2 = pos1 + macroname.length();
            if (pos2 >= macro.length())
                continue;
            if (macroparams.size())
            {
                if (code[pos2] != '(')
                    continue;

                int parlevel = 0;
                std::string par;
                for (; pos2 < code.length(); ++pos2)
                {
                    if (code[pos2] == '(')
                    {
                        ++parlevel;
                        if (parlevel == 1)
                            continue;
                    }
                    else if (code[pos2] == ')')
                    {
                        --parlevel;
                        if (parlevel <= 0)
                        {
                            params.push_back(par);
                            break;
                        }
                    }

                    if (parlevel == 1 && code[pos2] == ',')
                    {
                        params.push_back(par);
                        par = "";
                    }
                    else if (parlevel >= 1)
                    {
                        par += std::string(1, code[pos2]);
                    }
                }
            }

            // Same number of parameters..
            if (params.size() != macroparams.size())
                continue;

            // Create macro code..
            std::string macrocode;
            const Token *tok = tokenizer.tokens();
            if (! macroparams.empty())
            {
                while (tok && tok->str() != ")")
                    tok = tok->next();
            }
            while ((tok = tok->next()) != NULL)
            {
                std::string str = tok->str();
                if (tok->isName())
                {
                    for (unsigned int i = 0; i < macroparams.size(); ++i)
                    {
                        if (str == macroparams[i])
                        {
                            str = params[i];
                            break;
                        }
                    }
                }
                macrocode += str;
                if (Token::Match(tok, "%type% %var%"))
                    macrocode += " ";
            }

            // Insert macro code..
            code.erase(pos1, pos2 + 1 - pos1);
            code.insert(pos1, macrocode);
            pos1 += macrocode.length();
        }
    }

    return code;
}

