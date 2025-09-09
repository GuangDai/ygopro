#ifndef DECKMANAGER_H
#define DECKMANAGER_H

#include <unordered_map>
#include <vector>
#include <sstream>
#include <cstdlib> // for std::getenv
#include "data_manager.h"
#include "bufferio.h"

namespace ygo {

// 定义最大值常量，用于环境变量输入验证
constexpr int MAINC_MAX = 65535;
constexpr int SIDEC_MAX = 65535;

// 辅助函数：从环境变量中读取整数值
// 如果环境变量不存在、格式错误或超出范围，则返回默认值
inline int GetEnvInt(const char* name, int default_value) {
	const char* env_value = std::getenv(name);
	if (env_value) {
		char* end;
		long val = std::strtol(env_value, &end, 10);
		// 验证转换成功且值在合理范围内
		if (*end == '\0' && val >= 0 && val <= 65535) {
			return static_cast<int>(val);
		}
	}
	return default_value;
}

// DeckLimits 类：集中管理所有卡组限制
// 在程序启动时自动从环境变量初始化
struct DeckLimits {
	static int DECK_MAX_SIZE;    // 主卡组最大卡牌数
	static int DECK_MIN_SIZE;    // 主卡组最小卡牌数
	static int EXTRA_MAX_SIZE;   // 额外卡组最大卡牌数
	static int SIDE_MAX_SIZE;    // 侧卡组最大卡牌数
	static int MAX_CARD_COPIES;  // 单卡最大携带数量

	// 初始化函数：从环境变量读取配置
	static void Initialize() {
		DECK_MAX_SIZE = GetEnvInt("YGOPRO_MAX_DECK", 60);
		DECK_MIN_SIZE = GetEnvInt("YGOPRO_MIN_DECK", 40);
		EXTRA_MAX_SIZE = GetEnvInt("YGOPRO_MAX_EXTRA", 15);
		SIDE_MAX_SIZE = GetEnvInt("YGOPRO_MAX_SIDE", 15);
		MAX_CARD_COPIES = GetEnvInt("YGOPRO_MAX_COPIES", 3);

		// 确保数值逻辑正确：最小值不能大于最大值，所有值不能为负
		DECK_MIN_SIZE = std::max(1, std::min(DECK_MIN_SIZE, DECK_MAX_SIZE));
		DECK_MAX_SIZE = std::max(DECK_MIN_SIZE, DECK_MAX_SIZE);
		EXTRA_MAX_SIZE = std::max(0, EXTRA_MAX_SIZE);
		SIDE_MAX_SIZE = std::max(0, SIDE_MAX_SIZE);
		MAX_CARD_COPIES = std::max(0, MAX_CARD_COPIES);
	}
};

// 为方便在代码中直接使用，定义全局引用
// 这些引用指向 DeckLimits 类中的静态成员变量
extern int& DECK_MAX_SIZE;
extern int& DECK_MIN_SIZE;
extern int& EXTRA_MAX_SIZE;
extern int& SIDE_MAX_SIZE;
extern int& MAX_CARD_COPIES;

// 其他常量定义
constexpr int PACK_MAX_SIZE = 1000;

// 卡组分类常量
constexpr int DECK_CATEGORY_PACK = 0;
constexpr int DECK_CATEGORY_BOT = 1;
constexpr int DECK_CATEGORY_NONE = 2;
constexpr int DECK_CATEGORY_SEPARATOR = 3;
constexpr int DECK_CATEGORY_CUSTOM = 4;

// 禁限卡表结构
struct LFList {
	unsigned int hash{};
	std::wstring listName;
	std::unordered_map<uint32_t, int> content;
};

// 卡组结构：包含主卡组、额外卡组和侧卡组
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

// 用于保存卡组的简单结构（仅包含卡号）
struct DeckArray {
	std::vector<uint32_t> main;
	std::vector<uint32_t> extra;
	std::vector<uint32_t> side;
};

// 卡组管理器类
class DeckManager {
public:
	Deck current_deck; // 当前选中的卡组
	std::vector<LFList> _lfList; // 禁限卡表列表

#ifndef YGOPRO_SERVER_MODE
	static char deckBuffer[0x10000]; // 用于读取卡组文件的缓冲区
	static constexpr int MAX_YDK_SIZE = 0x10000;
#endif

	// 加载单个禁限卡表文件
	void LoadLFListSingle(const char* path, bool insert = false);
	void LoadLFListSingle(const wchar_t* path, bool insert = false);
#if defined(SERVER_ZIP_SUPPORT) || !defined(YGOPRO_SERVER_MODE)
	void LoadLFListSingle(irr::io::IReadFile* reader, bool insert = false);
#endif
	// 加载所有禁限卡表
	void LoadLFList();
	// 根据哈希值获取禁限卡表名称
	const wchar_t* GetLFListName(unsigned int lfhash);
	// 根据哈希值获取禁限卡表指针
	const LFList* GetLFList(unsigned int lfhash);
	// 检查卡组合法性
	unsigned int CheckDeck(const Deck& deck, unsigned int lfhash, int rule);

#ifndef YGOPRO_SERVER_MODE
	// 从文件或流加载当前卡组
	bool LoadCurrentDeck(const wchar_t* file, bool is_packlist = false);
	bool LoadCurrentDeck(int category_index, const wchar_t* category_name, const wchar_t* deckname);
	bool LoadCurrentDeck(std::istringstream& deckStream, bool is_packlist = false);
	wchar_t DeckFormatBuffer[128];
	// 统计某类卡牌在列表中的数量
	int TypeCount(std::vector<code_pointer> list, unsigned int ctype);
	// 从编码字符串加载卡组
	bool LoadDeckFromCode(Deck& deck, const unsigned char *code, int len);
	// 将卡组保存为编码字符串
	int SaveDeckToCode(Deck &deck, unsigned char *code);
#endif //YGOPRO_SERVER_MODE

	// 从数组加载卡组
	static uint32_t LoadDeck(Deck& deck, uint32_t dbuf[], int mainc, int sidec, bool is_packlist = false);
	// 加载侧卡组（用于换side）
	static bool LoadSide(Deck& deck, uint32_t dbuf[], int mainc, int sidec);

#ifndef YGOPRO_SERVER_MODE
	// 从输入流加载卡组
	static uint32_t LoadDeckFromStream(Deck& deck, std::istringstream& deckStream, bool is_packlist = false);
	// 获取分类路径
	static void GetCategoryPath(wchar_t* ret, int index, const wchar_t* text);
	// 获取卡组文件完整路径
	static void GetDeckFile(wchar_t* ret, int category_index, const wchar_t* category_name, const wchar_t* deckname);
	// 打开卡组文件
	static FILE* OpenDeckFile(const wchar_t* file, const char* mode);
	static irr::io::IReadFile* OpenDeckReader(const wchar_t* file);
	// 保存、删除卡组，创建、重命名、删除分类
	static bool SaveDeck(const Deck& deck, const wchar_t* file);
	static void SaveDeck(const Deck& deck, std::stringstream& deckStream);
	static bool DeleteDeck(const wchar_t* file);
	static bool CreateCategory(const wchar_t* name);
	static bool RenameCategory(const wchar_t* oldname, const wchar_t* newname);
	static bool DeleteCategory(const wchar_t* name);
	static bool SaveDeckArray(const DeckArray& deck, const wchar_t* name);
#endif //YGOPRO_SERVER_MODE

private:
	// 通用的从行提供器加载禁限卡表的模板函数
	template<typename LineProvider>
	void _LoadLFListFromLineProvider(LineProvider getLine, bool insert = false) {
		std::vector<LFList> loadedLists;
		auto cur = loadedLists.rend(); // 注意：在临时 list 上操作
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

// 声明全局的卡组管理器实例
extern DeckManager deckManager;

} // namespace ygo

#endif //DECKMANAGER_H
