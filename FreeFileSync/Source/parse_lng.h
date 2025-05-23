// *****************************************************************************
// * This file is part of the FreeFileSync project. It is distributed under    *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0          *
// * Copyright (C) Zenju (zenju AT freefilesync DOT org) - All Rights Reserved *
// *****************************************************************************

#ifndef PARSE_LNG_H_46794693622675638
#define PARSE_LNG_H_46794693622675638

#include <unordered_map>
#include <unordered_set>
#include <zen/utf.h>
#include "parse_plural.h"


namespace lng
{
//singular forms
using TranslationMap = std::unordered_map<std::string, std::string>; //orig |-> translation

//plural forms
using SingularPluralPair   = std::pair<std::string, std::string>; //1 house | %x houses
using PluralForms          = std::vector<std::string>; //1 dom | 2 domy | %x domów
using TranslationPluralMap = std::unordered_map<SingularPluralPair, PluralForms>; //(sing/plu) |-> pluralforms

struct TransHeader
{
    std::string languageName;     //display name: "English (UK)"
    std::string translatorName;   //"Zenju"
    std::string locale;           //ISO 639 language code + (optional) ISO 3166 country code, e.g. "de", "en_GB", or "en_US"
    std::string flagFile;         //"england.png"
    int pluralCount = 0;          //2
    std::string pluralDefinition; //"n == 1 ? 0 : 1"
};

struct ParsingError
{
    std::wstring msg;
    size_t row = 0; //starting with 0
    size_t col = 0; //
};
TransHeader parseHeader(const std::string& byteStream); //throw ParsingError
void parseLng(const std::string& byteStream, TransHeader& header, TranslationMap& out, TranslationPluralMap& pluralOut); //throw ParsingError

class TranslationUnorderedList; //unordered list of unique translation items
std::string generateLng(const TranslationUnorderedList& in, const TransHeader& header, bool untranslatedToTop);



















//--------------------------- implementation ---------------------------
}

template<> struct std::hash<lng::SingularPluralPair>
{
    size_t operator()(const lng::SingularPluralPair& str) const
    {
        zen::FNV1aHash<size_t> hash2; //shut up "GCC: shadow declaration"
        for (const char c : str.first ) hash2.add(c);
        for (const char c : str.second) hash2.add(c);
        return hash2.get();
    }
};

namespace lng
{
class TranslationUnorderedList //unordered list of unique translation items
{
public:
    TranslationUnorderedList(TranslationMap&& transOld, TranslationPluralMap&& transPluralOld) :
        transOld_(std::move(transOld)), transPluralOld_(std::move(transPluralOld)) {}

    void addItem(const std::string& orig)
    {
        if (!transUnique_.insert(orig).second) return;
        auto it = transOld_.find(orig);
        if (it != transOld_.end() && !it->second.empty()) //preserve old translation from .lng file if existing
            sequence_.push_back(std::make_shared<SingularItem>(std::make_pair(orig, it->second)));
        else
            sequence_.push_back(std::make_shared<SingularItem>(std::make_pair(orig, std::string())));
    }

    void addItem(const SingularPluralPair& orig)
    {
        if (!pluralUnique_.insert(orig).second) return;
        auto it = transPluralOld_.find(orig);
        if (it != transPluralOld_.end() && !it->second.empty()) //preserve old translation from .lng file if existing
            sequence_.push_back(std::make_shared<PluralItem>(std::make_pair(orig, it->second)));
        else
            sequence_.push_back(std::make_shared<PluralItem>(std::make_pair(orig, PluralForms())));
    }

    bool untranslatedTextExists() const { return std::any_of(sequence_.begin(), sequence_.end(), [](const std::shared_ptr<Item>& item) { return !item->hasTranslation(); }); }

    template <class Function, class Function2>
    void visitItems(Function onTrans, Function2 onPluralTrans) const //onTrans takes (const TranslationMap::value_type&), onPluralTrans takes (const TranslationPluralMap::value_type&)
    {
        for (const std::shared_ptr<Item>& item : sequence_)
            if (auto regular = dynamic_cast<const SingularItem*>(item.get()))
                onTrans(regular->value);
            else if (auto plural = dynamic_cast<const PluralItem*>(item.get()))
                onPluralTrans(plural->value);
            else assert(false);
    }

private:
    struct Item { virtual ~Item() {} virtual bool hasTranslation() const = 0; };

    struct SingularItem : public Item
    {
        explicit SingularItem(const TranslationMap::value_type& val) : value(val) {}
        bool hasTranslation() const override { return !value.second.empty(); }
        TranslationMap::value_type value;
    };

    struct PluralItem : public Item
    {
        explicit PluralItem(const TranslationPluralMap::value_type& val) : value(val) {}
        bool hasTranslation() const override { return !value.second.empty(); }
        TranslationPluralMap::value_type value;
    };

    std::vector<std::shared_ptr<Item>> sequence_; //ordered list of translation elements

    std::unordered_set<TranslationMap      ::key_type> transUnique_;  //check uniqueness
    std::unordered_set<TranslationPluralMap::key_type> pluralUnique_; //

    const TranslationMap transOld_;             //reuse existing translation
    const TranslationPluralMap transPluralOld_; //
};


enum class TokenType
{
    header,
    source,
    target,
    empty,
    text,
    plural,
    end,
};

struct Token
{
    Token(TokenType t) : type(t) {}

    TokenType type;
    std::string text;
};


class KnownTokens
{
public:
    KnownTokens() {} //clang wants it, clang gets it

    using TokenMap = std::unordered_map<TokenType, std::string>;

    const TokenMap& getList() const { return tokens_; }

    std::string text(TokenType t) const
    {
        auto it = tokens_.find(t);
        if (it != tokens_.end())
            return it->second;
        assert(false);
        return std::string();
    }

private:
    const TokenMap tokens_ =
    {
        {TokenType::header, "<header>"},
        {TokenType::source, "<source>"},
        {TokenType::target, "<target>"},
        {TokenType::empty,  "<empty>"},
        {TokenType::plural, "<pluralform>"},
    };
};


class Scanner
{
public:
    explicit Scanner(const std::string& byteStream) : stream_(byteStream), pos_(stream_.begin())
    {
        if (zen::startsWith(stream_, zen::BYTE_ORDER_MARK_UTF8))
            pos_ += zen::strLength(zen::BYTE_ORDER_MARK_UTF8);
    }

    Token getNextToken()
    {
        //skip whitespace
        pos_ = std::find_if_not(pos_, stream_.end(), zen::isWhiteSpace<char>);

        if (pos_ == stream_.end())
            return Token(TokenType::end);

        for (const auto& [tokenEnum, tokenString] : tokens_.getList())
            if (startsWith(tokenString))
            {
                pos_ += tokenString.size();
                return Token(tokenEnum);
            }

        //otherwise assume "text"
        auto itBegin = pos_;
        while (pos_ != stream_.end() && !startsWithKnownTag())
            pos_ = std::find(pos_ + 1, stream_.end(), '<');

        std::string text(itBegin, pos_);

        normalize(text); //remove whitespace from end etc.

        if (text.empty() && pos_ == stream_.end())
            return Token(TokenType::end);

        Token out(TokenType::text);
        out.text = std::move(text);
        return out;
    }

    size_t posRow() const //current row beginning with 0
    {
        //count line endings
        const size_t crSum = std::count(stream_.begin(), pos_, '\r'); //carriage returns
        const size_t nlSum = std::count(stream_.begin(), pos_, '\n'); //new lines
        assert(crSum == 0 || nlSum == 0 || crSum == nlSum);
        return std::max(crSum, nlSum); //be compatible with Linux/Mac/Win
    }

    size_t posCol() const //current col beginning with 0
    {
        //seek beginning of line
        for (auto it = pos_; it != stream_.begin(); )
        {
            --it;
            if (zen::isLineBreak(*it))
                return pos_ - it - 1;
        }
        return pos_ - stream_.begin();
    }

private:
    bool startsWithKnownTag() const
    {
        return std::any_of(tokens_.getList().begin(), tokens_.getList().end(),
        [&](const KnownTokens::TokenMap::value_type& p) { return startsWith(p.second); });
    }

    bool startsWith(const std::string& prefix) const
    {
        return zen::startsWith(zen::makeStringView(pos_, stream_.end()), prefix);
    }

    static void normalize(std::string& text)
    {
        zen::trim(text); //remove whitespace from both ends

        //Delimiter:
        //----------
        //Linux: 0xA        \n
        //Mac:   0xD        \r
        //Win:   0xD 0xA    \r\n    <- language files are in Windows format
        zen::replace(text, "\r\n", '\n'); //
        zen::replace(text, '\r',   '\n'); //ensure c-style line breaks
    }

    const std::string stream_;
    std::string::const_iterator pos_;
    const KnownTokens tokens_; //no need for static non-POD!
};


class LngParser
{
public:
    explicit LngParser(const std::string& byteStream) : scn_(byteStream), tk_(scn_.getNextToken()) {}

    void parse(TranslationMap& out, TranslationPluralMap& pluralOut, TransHeader& header) //throw ParsingError
    {
        parseHeader(header); //throw ParsingError

        try
        {
            plural::PluralFormInfo pi(header.pluralDefinition, header.pluralCount); //throw InvalidPluralForm

            while (token().type != TokenType::end)
                parseRegular(out, pluralOut, pi); //throw ParsingError
        }
        catch (const plural::InvalidPluralForm&)
        {
            throw ParsingError({L"Invalid plural form definition", scn_.posRow(), scn_.posCol()});
        }
    }

    void parseHeader(TransHeader& header) //throw ParsingError
    {
        using namespace zen;

        consumeToken(TokenType::header); //throw ParsingError

        const std::string headerRaw = token().text;
        consumeToken(TokenType::text); //throw ParsingError

        std::unordered_map<std::string_view, std::string_view> items;

        split2(headerRaw, [](const char c) { return isLineBreak(c); },
        [&items](const std::string_view block)
        {
            const std::string_view name = trimCpy(beforeFirst(block, ':', IfNotFoundReturn::none));
            if (!name.empty())
                items.emplace(name, trimCpy(afterFirst(block, ':', IfNotFoundReturn::none)));
        });

        auto getValue = [&](const std::string_view name)
        {
            auto it = items.find(name);
            if (it == items.end())
                throw ParsingError({replaceCpy<std::wstring>(L"Cannot find header item \"%x:\"", L"%x", utfTo<std::wstring>(name)), scn_.posRow(), scn_.posCol()});
            return it->second;
        };

        header.languageName     = getValue("language"); //throw ParsingError
        header.locale           = getValue("locale"); //throw ParsingError
        header.flagFile         = getValue("image"); //throw ParsingError
        header.pluralCount      = stringTo<int>(getValue("plural_count")); //throw ParsingError
        header.pluralDefinition = getValue("plural_definition"); //throw ParsingError
        header.translatorName   = getValue("translator"); //throw ParsingError
    }

private:
    void parseRegular(TranslationMap& out, TranslationPluralMap& pluralOut, const plural::PluralFormInfo& pluralInfo) //throw ParsingError
    {
        consumeToken(TokenType::source); //throw ParsingError

        if (token().type == TokenType::plural)
            return parsePlural(pluralOut, pluralInfo); //throw ParsingError

        std::string original = token().text;
        consumeToken(TokenType::text); //throw ParsingError

        consumeToken(TokenType::target); //throw ParsingError
        std::string translation;
        if (token().type == TokenType::text)
        {
            translation = token().text;
            nextToken();
        }
        else
            consumeToken(TokenType::empty); //throw ParsingError

        validateTranslation(original, translation); //throw ParsingError

        out.emplace(std::move(original), std::move(translation));
    }

    void parsePlural(TranslationPluralMap& pluralOut, const plural::PluralFormInfo& pluralInfo) //throw ParsingError
    {
        //TokenType::source already consumed

        consumeToken(TokenType::plural); //throw ParsingError
        std::string engSingular = token().text;
        consumeToken(TokenType::text); //throw ParsingError

        consumeToken(TokenType::plural); //throw ParsingError
        std::string engPlural = token().text;
        consumeToken(TokenType::text); //throw ParsingError

        const SingularPluralPair original(engSingular, engPlural);

        consumeToken(TokenType::target); //throw ParsingError

        PluralForms pluralList;
        while (token().type == TokenType::plural)
        {
            nextToken();
            std::string pluralForm = token().text;
            consumeToken(TokenType::text); //throw ParsingError

            pluralList.push_back(pluralForm);
        }

        if (pluralList.empty())
            consumeToken(TokenType::empty); //throw ParsingError

        validateTranslation(original, pluralList, pluralInfo);

        pluralOut.emplace(original, std::move(pluralList));
    }

    void validateTranslation(const std::string& original, const std::string& translation) //throw ParsingError
    {
        using namespace zen;

        if (original.empty())
            throw ParsingError({L"Translation source text is empty", scn_.posRow(), scn_.posCol()});

        if (!isValidUtf(original))
            throw ParsingError({L"Translation source text contains UTF-8 encoding error", scn_.posRow(), scn_.posCol()});
        if (!isValidUtf(translation))
            throw ParsingError({L"Translation text contains UTF-8 encoding error", scn_.posRow(), scn_.posCol()});

        if (!translation.empty())
        {
            //if original contains placeholder, so must translation!
            auto checkPlaceholder = [&](const std::string& placeholder)
            {
                if (contains(original, placeholder) &&
                    !contains(translation, placeholder))
                    throw ParsingError({replaceCpy<std::wstring>(L"Placeholder %x missing in translation", L"%x", utfTo<std::wstring>(placeholder)), scn_.posRow(), scn_.posCol()});
            };
            checkPlaceholder("%x");
            checkPlaceholder("%y");
            checkPlaceholder("%z");

            //if source is a one-liner, so should be the translation
            if (!contains(original, '\n') && contains(translation, '\n'))
                throw ParsingError({L"Source text is a one-liner, but translation consists of multiple lines", scn_.posRow(), scn_.posCol()});

            //if source contains ampersand to mark menu accellerator key, so must translation
            const size_t ampCount = ampersandTokenCount(original);
            if (ampCount > 1 || ampCount != ampersandTokenCount(translation))
                throw ParsingError({L"Source and translation both need exactly one & character to mark a menu item access key or none at all", scn_.posRow(), scn_.posCol()});

            //ampersand at the end makes buggy wxWidgets crash miserably
            if (endsWithSingleAmp(original) || endsWithSingleAmp(translation))
                throw ParsingError({L"The & character to mark a menu item access key must not occur at the end of a string", scn_.posRow(), scn_.posCol()});

            //if source ends with colon, so must translation
            if (endsWithColon(original) && !endsWithColon(translation))
                throw ParsingError({L"Source text ends with a colon character \":\", but translation does not", scn_.posRow(), scn_.posCol()});

            //if source ends with period, so must translation
            if (endsWithSingleDot(original) && !endsWithSingleDot(translation))
                throw ParsingError({L"Source text ends with a punctuation mark character \".\", but translation does not", scn_.posRow(), scn_.posCol()});

            //if source ends with ellipsis, so must translation
            if (endsWithEllipsis(original) && !endsWithEllipsis(translation))
                throw ParsingError({L"Source text ends with an ellipsis \"...\", but translation does not", scn_.posRow(), scn_.posCol()});

            //check for not-to-be-translated texts
            for (const char* fixedStr : {"FreeFileSync", "RealTimeSync", "ffs_gui", "ffs_batch", "ffs_real", "ffs_tmp", "GlobalSettings.xml"})
                if (contains(original, fixedStr) && !contains(translation, fixedStr))
                    throw ParsingError({replaceCpy<std::wstring>(L"Misspelled \"%x\" in translation", L"%x", utfTo<std::wstring>(fixedStr)), scn_.posRow(), scn_.posCol()});

            //some languages (French!) put a space before punctuation mark => must be a no-brake space!
            for (const char punctChar : std::string_view(".!?:;$#"))
                if (contains(original,    std::string(" ") + punctChar) ||
                    contains(translation, std::string(" ") + punctChar))
                    throw ParsingError({replaceCpy<std::wstring>(L"Text contains a space before the \"%x\" character. Are line-breaks really allowed here?"
                                                                 L" Maybe this should be a \"non-breaking space\" (Windows: Alt 0160    UTF8: 0xC2 0xA0)?",
                                                                 L"%x", utfTo<std::wstring>(punctChar)), scn_.posRow(), scn_.posCol()});
        }
    }

    void validateTranslation(const SingularPluralPair& original, const PluralForms& translation, const plural::PluralFormInfo& pluralInfo) //throw ParsingError
    {
        using namespace zen;

        if (original.first.empty() || original.second.empty())
            throw ParsingError({L"Translation source text is empty", scn_.posRow(), scn_.posCol()});

        const std::vector<std::string> allTexts = [&]
        {
            std::vector<std::string> at{original.first, original.second};
            at.insert(at.end(), translation.begin(), translation.end());
            return at;
        }();

        for (const std::string& str : allTexts)
            if (!isValidUtf(str))
                throw ParsingError({L"Text contains UTF-8 encoding error", scn_.posRow(), scn_.posCol()});

        //check the primary placeholder is existing at least for the second english text
        if (!contains(original.second, "%x"))
            throw ParsingError({L"Plural form source text does not contain %x placeholder", scn_.posRow(), scn_.posCol()});

        if (!translation.empty())
        {
            //check for invalid number of plural forms
            if (pluralInfo.getCount() != translation.size())
                throw ParsingError({replaceCpy(replaceCpy<std::wstring>(L"Invalid number of plural forms; actual: %x, expected: %y",
                                                                        L"%x", formatNumber(translation.size())),
                                               L"%y", formatNumber(pluralInfo.getCount())), scn_.posRow(), scn_.posCol()});

            //check for duplicate plural form translations (catch copy & paste errors for single-number form translations)
            for (auto it = translation.begin(); it != translation.end(); ++it)
                if (!contains(*it, "%x"))
                {
                    auto it2 = std::find(it + 1, translation.end(), *it);
                    if (it2 != translation.end())
                        throw ParsingError({replaceCpy<std::wstring>(L"Duplicate plural form translation at index position %x",
                                                                     L"%x", formatNumber(it2 - translation.begin())), scn_.posRow(), scn_.posCol()});
                }

            for (size_t pos = 0; pos < translation.size(); ++pos)
                if (pluralInfo.isSingleNumberForm(pos))
                {
                    //translation needs to use decimal number if english source does so (e.g. frequently changing text like statistics)
                    if (contains(original.first, "%x") ||
                        contains(original.first, "1"))
                    {
                        const int firstNumber = pluralInfo.getFirstNumber(pos);
                        if (!contains(translation[pos], "%x") &&
                            !contains(translation[pos], numberTo<std::string>(firstNumber)))
                            throw ParsingError({replaceCpy<std::wstring>(replaceCpy<std::wstring>(L"Plural form translation at index position %y needs to use the decimal number %z or the %x placeholder",
                                                                                                  L"%y", formatNumber(pos)), L"%z", formatNumber(firstNumber)), scn_.posRow(), scn_.posCol()});
                    }
                }
                else
                {
                    //ensure the placeholder is used when needed
                    if (!contains(translation[pos], "%x"))
                        throw ParsingError({replaceCpy<std::wstring>(L"Plural form at index position %y is missing the %x placeholder", L"%y", formatNumber(pos)), scn_.posRow(), scn_.posCol()});
                }

            auto checkSecondaryPlaceholder = [&](const std::string& placeholder)
            {
                //make sure secondary placeholder is used for both source texts (or none) and all plural forms
                if (contains(original.first,  placeholder) ||
                    contains(original.second, placeholder))
                    for (const std::string& str : allTexts)
                        if (!contains(str, placeholder))
                            throw ParsingError({replaceCpy<std::wstring>(L"Placeholder %x missing in text", L"%x", utfTo<std::wstring>(placeholder)), scn_.posRow(), scn_.posCol()});
            };
            checkSecondaryPlaceholder("%y");
            checkSecondaryPlaceholder("%z");

            //if source is a one-liner, so should be the translation
            if (!contains(original.first, '\n') && !contains(original.second, '\n') &&
            /**/std::any_of(translation.begin(), translation.end(), [](const std::string& pform) { return contains(pform, '\n'); }))
            /**/throw ParsingError({L"Source text is a one-liner, but at least one plural form translation consists of multiple lines", scn_.posRow(), scn_.posCol()});

            //if source contains ampersand to mark menu accellerator key, so must translation
            const size_t ampCount = ampersandTokenCount(original.first);
            for (const std::string& str : allTexts)
                if (ampCount > 1 || ampersandTokenCount(str) != ampCount)
                    throw ParsingError({L"Source and translation both need exactly one & character to mark a menu item access key or none at all", scn_.posRow(), scn_.posCol()});

            //ampersand at the end makes buggy wxWidgets crash miserably
            for (const std::string& str : allTexts)
                if (endsWithSingleAmp(str))
                    throw ParsingError({L"The & character to mark a menu item access key must not occur at the end of a string", scn_.posRow(), scn_.posCol()});

            //if source ends with colon, so must translation (note: character seems to be universally used, even for asian and arabic languages)
            if (endsWith(original.first, ':') || endsWith(original.second, ':'))
                for (const std::string& str : allTexts)
                    if (!endsWithColon(str))
                        throw ParsingError({L"Source text ends with a colon character \":\", but translation does not", scn_.posRow(), scn_.posCol()});

            //if source ends with a period, so must translation (note: character seems to be universally used, even for asian and arabic languages)
            if (endsWithSingleDot(original.first) || endsWithSingleDot(original.second))
                for (const std::string& str : allTexts)
                    if (!endsWithSingleDot(str))
                        throw ParsingError({L"Source text ends with a punctuation mark character \".\", but translation does not", scn_.posRow(), scn_.posCol()});

            //if source ends with an ellipsis, so must translation (note: character seems to be universally used, even for asian and arabic languages)
            if (endsWithEllipsis(original.first) || endsWithEllipsis(original.second))
                for (const std::string& str : allTexts)
                    if (!endsWithEllipsis(str))
                        throw ParsingError({L"Source text ends with an ellipsis \"...\", but translation does not", scn_.posRow(), scn_.posCol()});

            //check for not-to-be-translated texts
            for (const char* fixedStr : {"FreeFileSync", "RealTimeSync", "ffs_gui", "ffs_batch", "ffs_tmp", "GlobalSettings.xml"})
                if (contains(original.first, fixedStr) || contains(original.second, fixedStr))
                    for (const std::string& str : allTexts)
                        if (!contains(str, fixedStr))
                            throw ParsingError({replaceCpy<std::wstring>(L"Misspelled \"%x\" in translation", L"%x", utfTo<std::wstring>(fixedStr)), scn_.posRow(), scn_.posCol()});

            //some languages (French!) put a space before punctuation mark => must be a no-brake space!
            for (const char punctChar : std::string(".!?:;$#"))
                for (const std::string& str : allTexts)
                    if (contains(str, std::string(" ") + punctChar))
                        throw ParsingError({replaceCpy<std::wstring>(L"Text contains a space before the \"%x\" character. Are line-breaks really allowed here?"
                                                                     L" Maybe this should be a \"non-breaking space\" (Windows: Alt 0160    UTF8: 0xC2 0xA0)?",
                                                                     L"%x", utfTo<std::wstring>(punctChar)), scn_.posRow(), scn_.posCol()});
        }
    }

    static size_t ampersandTokenCount(const std::string& str)
    {
        using namespace zen;
        const std::string tmp = replaceCpy(str, "&&", ""); //make sure to not catch && which windows resolves as just one & for display!
        return std::count(tmp.begin(), tmp.end(), '&');
    }

    static bool endsWithSingleAmp(const std::string& s)
    {
        using namespace zen;
        return endsWith(s, "&") && !endsWith(s, "&&");
    }

    static bool endsWithEllipsis(const std::string& s)
    {
        using namespace zen;
        return endsWith(s, "...") ||
               endsWith(s, "\xe2\x80\xa6"); //narrow ellipsis (spanish?)
    }

    static bool endsWithColon(const std::string& s)
    {
        using namespace zen;
        return endsWith(s, ':') ||
               endsWith(s, "\xef\xbc\x9a"); //chinese colon
    }

    static bool endsWithSingleDot(const std::string& s)
    {
        using namespace zen;
        return (endsWith(s, ".")            ||
                endsWith(s, "\xe0\xa5\xa4") || //hindi period
                endsWith(s, "\xe3\x80\x82")) //chinese period
               &&
               (!endsWith(s, "..")                      &&
                !endsWith(s, "\xe0\xa5\xa4" "\xe0\xa5\xa4") && //hindi period
                !endsWith(s, "\xe3\x80\x82" "\xe3\x80\x82")); //chinese period
    }


    const Token& token() const { return tk_; }

    void nextToken() { tk_ = scn_.getNextToken(); }

    void expectToken(TokenType t) //throw ParsingError
    {
        if (token().type != t)
            throw ParsingError({L"Unexpected token", scn_.posRow(), scn_.posCol()});
    }

    void consumeToken(TokenType t) //throw ParsingError
    {
        expectToken(t); //throw ParsingError
        nextToken();
    }

    Scanner scn_;
    Token tk_;
};


inline
void parseLng(const std::string& byteStream, TransHeader& header, TranslationMap& out, TranslationPluralMap& pluralOut) //throw ParsingError
{
    out.clear();
    pluralOut.clear();

    LngParser(byteStream).parse(out, pluralOut, header); //throw ParsingError
}


inline
TransHeader parseHeader(const std::string& byteStream) //throw ParsingError
{
    TransHeader header;
    LngParser(byteStream).parseHeader(header); //throw ParsingError
    return header;
}


inline
std::string generateLng(const TranslationUnorderedList& in, const TransHeader& header, bool untranslatedToTop)
{
    using namespace zen;

    const KnownTokens tokens; //no need for static non-POD!

    std::string headerLines;
    headerLines += tokens.text(TokenType::header) + '\n';

    headerLines += "\t" "language: " + header.languageName + '\n';
    headerLines += "\t" "locale: "   + header.locale + '\n';
    headerLines += "\t" "image: "    + header.flagFile + '\n';
    headerLines += "\t" "plural_count: "      + numberTo<std::string>(header.pluralCount) + '\n';
    headerLines += "\t" "plural_definition: " + header.pluralDefinition + '\n';
    headerLines += "\t" "translator: " + header.translatorName;


    std::string topLines; //untranslated items first?
    std::string mainLines;

    in.visitItems([&](const TranslationMap::value_type& trans)
    {
        std::string& out = untranslatedToTop && trans.second.empty() ? topLines : mainLines;

        std::string original    = trans.first;
        std::string translation = trans.second;

        out += + "\n\n" + tokens.text(TokenType::source) + ' ' + original + '\n';

        if (contains(original, '\n')) //multiple lines
            out += + '\n';

        out += tokens.text(TokenType::target) + ' ' + translation;

        if (translation.empty()) //help translators search for untranslated items
            out += tokens.text(TokenType::empty);
    },
    [&](const TranslationPluralMap::value_type& transPlural)
    {
        std::string& out = untranslatedToTop && transPlural.second.empty() ? topLines : mainLines;

        std::string engSingular  = transPlural.first.first;
        std::string engPlural    = transPlural.first.second;
        const PluralForms& forms = transPlural.second;

        out += "\n\n" + tokens.text(TokenType::source) + '\n';
        out += '\t' + tokens.text(TokenType::plural) + ' ' + engSingular + '\n';
        out += '\t' + tokens.text(TokenType::plural) + ' ' + engPlural   + '\n';

        out += tokens.text(TokenType::target);

        for (std::string plForm : forms)
            out += + "\n\t" + tokens.text(TokenType::plural) + ' ' + plForm;

        if (forms.empty()) //help translators search for untranslated items
            out += ' ' + tokens.text(TokenType::empty);
    });

    std::string output = headerLines + topLines + mainLines;
    assert(!contains(output, "\r\n") && !contains(output, "\r"));
    return replaceCpy(output, '\n', "\r\n"); //back to Windows line endings
}
}

#endif //PARSE_LNG_H_46794693622675638
