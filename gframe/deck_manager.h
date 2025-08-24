#ifndef DECKMANAGER_H
#define DECKMANAGER_H

#include <unordered_map>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include "data_manager.h"
#include "bufferio.h"

namespace ygo {

constexpr int MAINC_MAX = 65535;
constexpr int SIDEC_MAX = 65535;

// 辅助函数：从环境变量获取整数值，如果不存在则返回默认值
inline int GetEnvInt(const char* name, int default_value) {
	const char* env_value = std::getenv(name);
	if (env_value) {
		char* end;
		long val = std::strtol(env_value, &end, 10);
		if (*end == '\0' && val >= 0 && val <= 65535) {
			return static_cast<int>(val);
		}
	}
	return default_value;
}

// 使用静态初始化确保这些值在程序启动时就被设置
struct DeckLimits {
	static int DECK_MAX_SIZE;
	static int DECK_MIN_SIZE;
	static int EXTRA_MAX_SIZE;
	static int SIDE_MAX_SIZE;
	
	static void Initialize() {
		DECK_MAX_SIZE = GetEnvInt("YGOPRO_MAX_DECK", 60);
		DECK_MIN_SIZE = GetEnvInt("YGOPRO_MIN_DECK", 40);
		EXTRA_MAX_SIZE = GetEnvInt("YGOPRO_MAX_EXTRA", 15);
		SIDE_MAX_SIZE = GetEnvInt("YGOPRO_MAX_SIDE", 15);
		
		
		DECK_MIN_SIZE = std::max(1, std::min(DECK_MIN_SIZE, DECK_MAX_SIZE));
		DECK_MAX_SIZE = std::max(DECK_MIN_SIZE, DECK_MAX_SIZE);
		EXTRA_MAX_SIZE = std::max(0, EXTRA_MAX_SIZE);
		SIDE_MAX_SIZE = std::max(0, SIDE_MAX_SIZE);
	}
};


extern int& DECK_MAX_SIZE;
extern int& DECK_MIN_SIZE;
extern int& EXTRA_MAX_SIZE;
extern int& SIDE_MAX_SIZE;

constexpr int PACK_MAX_SIZE = 1000;

struct LFList {
	unsigned int hash{};
	std::wstring listName;
	std::unordered_map<uint32_t, int> content;
};

struct Deck {
	std::vector<code_pointer> main;
	std::vector<code_pointer> extra;
	std::vector<code_pointer> side;
	Deck() = default;
	Deck(const Deck& ndeck) {
		main = ndeck.main;
		extra = ndeck.extra;
		side = ndeck.side;
	}
	void clear() {
		main.clear();
		extra.clear();
		side.clear();
	}
};

struct DeckArray {
	std::vector<uint32_t> main;
	std::vector<uint32_t> extra;
	std::vector<uint32_t> side;
};

class DeckManager {
public:
	Deck current_deck;
	std::vector<LFList> _lfList;

#ifndef YGOPRO_SERVER_MODE
	static char deckBuffer[0x10000];
#endif

	void LoadLFListSingle(const char* path, bool insert = false);
	void LoadLFListSingle(const wchar_t* path, bool insert = false);
#if defined(SERVER_ZIP_SUPPORT) || !defined(YGOPRO_SERVER_MODE)
	void LoadLFListSingle(irr::io::IReadFile* reader, bool insert = false);
#endif
	void LoadLFList();
	const wchar_t* GetLFListName(unsigned int lfhash);
	const LFList* GetLFList(unsigned int lfhash);
	unsigned int CheckDeck(const Deck& deck, unsigned int lfhash, int rule);
#ifndef YGOPRO_SERVER_MODE
	bool LoadCurrentDeck(const wchar_t* file, bool is_packlist = false);
	bool LoadCurrentDeck(int category_index, const wchar_t* category_name, const wchar_t* deckname);
	bool LoadCurrentDeck(std::istringstream& deckStream, bool is_packlist = false);
	wchar_t DeckFormatBuffer[128];
	int TypeCount(std::vector<code_pointer> list, unsigned int ctype);
	bool LoadDeckFromCode(Deck& deck, const unsigned char *code, int len);
	int SaveDeckToCode(Deck &deck, unsigned char *code);
#endif //YGOPRO_SERVER_MODE

	static uint32_t LoadDeck(Deck& deck, uint32_t dbuf[], int mainc, int sidec, bool is_packlist = false);
	static bool LoadSide(Deck& deck, uint32_t dbuf[], int mainc, int sidec);
#ifndef YGOPRO_SERVER_MODE
	static uint32_t LoadDeckFromStream(Deck& deck, std::istringstream& deckStream, bool is_packlist = false);
	static void GetCategoryPath(wchar_t* ret, int index, const wchar_t* text);
	static void GetDeckFile(wchar_t* ret, int category_index, const wchar_t* category_name, const wchar_t* deckname);
	static FILE* OpenDeckFile(const wchar_t* file, const char* mode);
	static irr::io::IReadFile* OpenDeckReader(const wchar_t* file);
	static bool SaveDeck(const Deck& deck, const wchar_t* file);
	static void SaveDeck(const Deck& deck, std::stringstream& deckStream);
	static bool DeleteDeck(const wchar_t* file);
	static bool CreateCategory(const wchar_t* name);
	static bool RenameCategory(const wchar_t* oldname, const wchar_t* newname);
	static bool DeleteCategory(const wchar_t* name);
	static bool SaveDeckArray(const DeckArray& deck, const wchar_t* name);
#endif //YGOPRO_SERVER_MODE

private:
	template<typename LineProvider>
	void _LoadLFListFromLineProvider(LineProvider getLine, bool insert = false) {
		std::vector<LFList> loadedLists;
		auto cur = loadedLists.rend();
		char linebuf[256]{};
		wchar_t strBuffer[256]{};

		while (getLine(linebuf, sizeof(linebuf))) {
			if (linebuf[0] == '#')
				continue;
			if (linebuf[0] == '!') {
				auto len = std::strcspn(linebuf, "\r\n");
				linebuf[len] = 0;
				BufferIO::DecodeUTF8(&linebuf[1], strBuffer);
				LFList newlist;
				newlist.listName = strBuffer;
				newlist.hash = 0x7dfcee6a;
				loadedLists.push_back(newlist);
				cur = loadedLists.rbegin();
				continue;
			}
			if (cur == loadedLists.rend())
				continue;
			char* pos = linebuf;
			errno = 0;
			auto result = std::strtoul(pos, &pos, 10);
			if (errno || result > UINT32_MAX)
				continue;
			if (pos == linebuf || *pos != ' ')
				continue;
			uint32_t code = static_cast<uint32_t>(result);
			errno = 0;
			int count = std::strtol(pos, &pos, 10);
			if (errno)
				continue;
			if (count < 0 || count > 2)
				continue;
			cur->content[code] = count;
			cur->hash = cur->hash ^ ((code << 18) | (code >> 14)) ^ ((code << (27 + count)) | (code >> (5 - count)));
		}

		if (insert) {
			_lfList.insert(_lfList.begin(), loadedLists.begin(), loadedLists.end());
		} else {
			_lfList.insert(_lfList.end(), loadedLists.begin(), loadedLists.end());
		}
	}
};

extern DeckManager deckManager;

}

#ifdef YGOPRO_SERVER_MODE
#define DECKCOUNT_MAIN_MIN ygo::DECK_MIN_SIZE
#define DECKCOUNT_MAIN_MAX ygo::DECK_MAX_SIZE
#define DECKCOUNT_SIDE ygo::SIDE_MAX_SIZE
#define DECKCOUNT_EXTRA ygo::EXTRA_MAX_SIZE

#endif //YGOPRO_SERVER_MODE

#endif //DECKMANAGER_H
