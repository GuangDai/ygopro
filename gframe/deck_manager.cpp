#include "deck_manager.h"
#include "game.h"
#include "myfilesystem.h"
#include "network.h"
#include "base64.h"

namespace ygo {

// DeckLimits 静态成员变量的定义
int DeckLimits::DECK_MAX_SIZE = 60;
int DeckLimits::DECK_MIN_SIZE = 40;
int DeckLimits::EXTRA_MAX_SIZE = 15;
int DeckLimits::SIDE_MAX_SIZE = 15;
int DeckLimits::MAX_CARD_COPIES = 3;

// 全局引用的定义
int& DECK_MAX_SIZE = DeckLimits::DECK_MAX_SIZE;
int& DECK_MIN_SIZE = DeckLimits::DECK_MIN_SIZE;
int& EXTRA_MAX_SIZE = DeckLimits::EXTRA_MAX_SIZE;
int& SIDE_MAX_SIZE = DeckLimits::SIDE_MAX_SIZE;
int& MAX_CARD_COPIES = DeckLimits::MAX_CARD_COPIES;

// 静态初始化器：确保在程序启动时（任何函数调用前）初始化卡组限制
static struct DeckLimitsInitializer {
	DeckLimitsInitializer() {
		DeckLimits::Initialize();
	}
} deckLimitsInitializer;

#ifndef YGOPRO_SERVER_MODE
// 静态成员变量的定义
char DeckManager::deckBuffer[0x10000]{};
#endif

// 全局实例的定义
DeckManager deckManager;

// 从文件句柄加载禁限卡表
void DeckManager::LoadLFListSingle(const char* path, bool insert) {
	FILE* fp = myfopen(path, "r");
	if (!fp) return;
	_LoadLFListFromLineProvider([&](char* buf, size_t sz) {
		return std::fgets(buf, sz, fp) != nullptr;
	}, insert);
	std::fclose(fp);
}

void DeckManager::LoadLFListSingle(const wchar_t* path, bool insert) {
	FILE* fp = mywfopen(path, "r");
	if (!fp) return;
	_LoadLFListFromLineProvider([&](char* buf, size_t sz) {
		return std::fgets(buf, sz, fp) != nullptr;
	}, insert);
	std::fclose(fp);
}

#if defined(SERVER_ZIP_SUPPORT) || !defined(YGOPRO_SERVER_MODE)
void DeckManager::LoadLFListSingle(irr::io::IReadFile* reader, bool insert) {
	std::string linebuf;
	char ch{};
	_LoadLFListFromLineProvider([&](char* buf, size_t sz) {
		while (reader->read(&ch, 1)) {
			if (ch == '\0') break;
			linebuf.push_back(ch);
			if (ch == '\n' || linebuf.size() >= sz - 1) {
				std::strncpy(buf, linebuf.c_str(), sz - 1);
				buf[sz - 1] = '\0';
				linebuf.clear();
				return true;
			}
		}
		return false;
	}, insert);
	reader->drop();
}
#endif

// 加载所有预设的禁限卡表文件
void DeckManager::LoadLFList() {
#ifdef SERVER_PRO2_SUPPORT
	LoadLFListSingle("config/lflist.conf");
#elif defined(SERVER_PRO3_SUPPORT)
	LoadLFListSingle("Data/lflist.conf");
#endif
	LoadLFListSingle("specials/lflist.conf");
	LoadLFListSingle("lflist.conf");
	// 添加一个“无限制”的选项
	LFList nolimit;
	nolimit.listName = L"N/A";
	nolimit.hash = 0;
	_lfList.push_back(nolimit);
}

// 根据哈希值获取禁限卡表名称
const wchar_t* DeckManager::GetLFListName(unsigned int lfhash) {
	auto lit = std::find_if(_lfList.begin(), _lfList.end(), [lfhash](const ygo::LFList& list) {
		return list.hash == lfhash;
	});
	if(lit != _lfList.end())
		return lit->listName.c_str();
	return dataManager.unknown_string;
}

// 根据哈希值获取禁限卡表指针
const LFList* DeckManager::GetLFList(unsigned int lfhash) {
	auto lit = std::find_if(_lfList.begin(), _lfList.end(), [lfhash](const ygo::LFList& list) {
		return list.hash == lfhash;
	});
	if (lit != _lfList.end())
		return &(*lit);
	return nullptr;
}

// 辅助函数：检查卡片是否符合当前游戏规则（OCG/TCG等）
static unsigned int checkAvail(unsigned int ot, unsigned int avail) {
	if(!!(ot & 0x4))
		return 0;
	if((ot & avail) == avail)
		return 0;
	if((ot & AVAIL_OCG) && (avail != AVAIL_OCG))
		return DECKERROR_OCGONLY;
	if((ot & AVAIL_TCG) && (avail != AVAIL_TCG))
		return DECKERROR_TCGONLY;
	return DECKERROR_NOTAVAIL;
}

// 检查卡组是否合法
unsigned int DeckManager::CheckDeck(const Deck& deck, unsigned int lfhash, int rule) {
	std::unordered_map<int, int> ccount;
	// 规则1: 检查卡组大小
	if((int)deck.main.size() < DECK_MIN_SIZE || (int)deck.main.size() > DECK_MAX_SIZE)
		return (DECKERROR_MAINCOUNT << 28) | (unsigned)deck.main.size();
	if((int)deck.extra.size() > EXTRA_MAX_SIZE)
		return (DECKERROR_EXTRACOUNT << 28) | (unsigned)deck.extra.size();
	if((int)deck.side.size() > SIDE_MAX_SIZE)
		return (DECKERROR_SIDECOUNT << 28) | (unsigned)deck.side.size();

	// 规则2: 获取禁限卡表
	auto lflist = GetLFList(lfhash);
	if (!lflist)
		return 0;
	auto& list = lflist->content;

	// 规则3: 检查卡片可用性（OCG/TCG）
	const unsigned int rule_map[6] = { AVAIL_OCG, AVAIL_TCG, AVAIL_SC, AVAIL_CUSTOM, AVAIL_OCGTCG, 0 };
	unsigned int avail = 0;
	if (rule >= 0 && rule < (int)(sizeof rule_map / sizeof rule_map[0]))
		avail = rule_map[rule];

	// 检查主卡组
	for (auto& cit : deck.main) {
		auto gameruleDeckError = checkAvail(cit->second.ot, avail);
		if(gameruleDeckError)
			return (gameruleDeckError << 28) | cit->first;
		// 主卡组不能放额外卡组或衍生物
		if (cit->second.type & (TYPES_EXTRA_DECK | TYPE_TOKEN))
			return (DECKERROR_MAINCOUNT << 28);
		int code = cit->second.alias ? cit->second.alias : cit->first;
		ccount[code]++;
		int dc = ccount[code];
		// 检查单卡数量
		if(dc > MAX_CARD_COPIES)
			return (DECKERROR_CARDCOUNT << 28) | cit->first;
		// 检查禁限卡表
		auto it = list.find(code);
		if(it != list.end() && dc > it->second)
			return (DECKERROR_LFLIST << 28) | cit->first;
	}

	// 检查额外卡组
	for (auto& cit : deck.extra) {
		auto gameruleDeckError = checkAvail(cit->second.ot, avail);
		if(gameruleDeckError)
			return (gameruleDeckError << 28) | cit->first;
		// 额外卡组必须是融合、同调、超量、灵摆、连接怪兽，且不能是衍生物
		if (!(cit->second.type & TYPES_EXTRA_DECK) || cit->second.type & TYPE_TOKEN)
			return (DECKERROR_EXTRACOUNT << 28);
		int code = cit->second.alias ? cit->second.alias : cit->first;
		ccount[code]++;
		int dc = ccount[code];
		if(dc > MAX_CARD_COPIES)
			return (DECKERROR_CARDCOUNT << 28) | cit->first;
		auto it = list.find(code);
		if(it != list.end() && dc > it->second)
			return (DECKERROR_LFLIST << 28) | cit->first;
	}

	// 检查侧卡组
	for (auto& cit : deck.side) {
		auto gameruleDeckError = checkAvail(cit->second.ot, avail);
		if(gameruleDeckError)
			return (gameruleDeckError << 28) | cit->first;
		// 侧卡组不能放衍生物
		if (cit->second.type & TYPE_TOKEN)
			return (DECKERROR_SIDECOUNT << 28);
		int code = cit->second.alias ? cit->second.alias : cit->first;
		ccount[code]++;
		int dc = ccount[code];
		if(dc > MAX_CARD_COPIES)
			return (DECKERROR_CARDCOUNT << 28) | cit->first;
		auto it = list.find(code);
		if(it != list.end() && dc > it->second)
			return (DECKERROR_LFLIST << 28) | cit->first;
	}

	return 0;
}

// 从卡号数组加载卡组
uint32_t DeckManager::LoadDeck(Deck& deck, uint32_t dbuf[], int mainc, int sidec, bool is_packlist) {
	deck.clear();
	uint32_t errorcode = 0;
	CardData cd;
	for(int i = 0; i < mainc; ++i) {
		auto code = dbuf[i];
		if(!dataManager.GetData(code, &cd)) {
			errorcode = code;
			continue;
		}
		if (cd.type & TYPE_TOKEN) {
			errorcode = code;
			continue;
		}
		if(is_packlist) {
			deck.main.push_back(dataManager.GetCodePointer(code));
			continue;
		}
		if (cd.type & TYPES_EXTRA_DECK) {
			if ((int)deck.extra.size() < EXTRA_MAX_SIZE)
				deck.extra.push_back(dataManager.GetCodePointer(code));
		}
		else {
			if ((int)deck.main.size() < DECK_MAX_SIZE)
				deck.main.push_back(dataManager.GetCodePointer(code));
		}
	}
	for(int i = 0; i < sidec; ++i) {
		auto code = dbuf[mainc + i];
		if(!dataManager.GetData(code, &cd)) {
			errorcode = code;
			continue;
		}
		if (cd.type & TYPE_TOKEN) {
			errorcode = code;
			continue;
		}
		if((int)deck.side.size() < SIDE_MAX_SIZE)
			deck.side.push_back(dataManager.GetCodePointer(code));
	}
	return errorcode;
}

#ifndef YGOPRO_SERVER_MODE
// 从输入流加载卡组
uint32_t DeckManager::LoadDeckFromStream(Deck& deck, std::istringstream& deckStream, bool is_packlist) {
	int ct = 0;
	int mainc = 0, sidec = 0;
	uint32_t cardlist[PACK_MAX_SIZE]{};
	bool is_side = false;
	std::string linebuf;
	while (std::getline(deckStream, linebuf, '\n') && ct < PACK_MAX_SIZE) {
		if (linebuf[0] == '!') {
			is_side = true;
			continue;
		}
		if (linebuf[0] < '0' || linebuf[0] > '9')
			continue;
		errno = 0;
		auto code = std::strtoul(linebuf.c_str(), nullptr, 10);
		if (errno || code > UINT32_MAX)
			continue;
		cardlist[ct++] = code;
		if (is_side)
			++sidec;
		else
			++mainc;
	}
	return LoadDeck(deck, cardlist, mainc, sidec, is_packlist);
}
#endif // YGOPRO_SERVER_MODE

// 加载侧卡组（用于换side功能）
bool DeckManager::LoadSide(Deck& deck, uint32_t dbuf[], int mainc, int sidec) {
	std::unordered_map<uint32_t, int> pcount;
	std::unordered_map<uint32_t, int> ncount;
	for(size_t i = 0; i < deck.main.size(); ++i)
		pcount[deck.main[i]->first]++;
	for(size_t i = 0; i < deck.extra.size(); ++i)
		pcount[deck.extra[i]->first]++;
	for(size_t i = 0; i < deck.side.size(); ++i)
		pcount[deck.side[i]->first]++;
	Deck ndeck;
	LoadDeck(ndeck, dbuf, mainc, sidec);
#ifndef YGOPRO_NO_SIDE_CHECK
	if(ndeck.main.size() != deck.main.size() || ndeck.extra.size() != deck.extra.size() || ndeck.side.size() != deck.side.size())
		return false;
#endif
	for(size_t i = 0; i < ndeck.main.size(); ++i)
		ncount[ndeck.main[i]->first]++;
	for(size_t i = 0; i < ndeck.extra.size(); ++i)
		ncount[ndeck.extra[i]->first]++;
	for(size_t i = 0; i < ndeck.side.size(); ++i)
		ncount[ndeck.side[i]->first]++;
#ifndef YGOPRO_NO_SIDE_CHECK
	for (auto& cdit : ncount)
		if (cdit.second != pcount[cdit.first])
			return false;
#endif
	deck = ndeck;
	return true;
}

#ifndef YGOPRO_SERVER_MODE
// 获取卡组分类的路径
void DeckManager::GetCategoryPath(wchar_t* ret, int index, const wchar_t* text) {
	wchar_t catepath[256];
	switch(index) {
	case DECK_CATEGORY_PACK:
		myswprintf(catepath, L"./pack");
		break;
	case DECK_CATEGORY_BOT:
		BufferIO::CopyWideString(mainGame->gameConf.bot_deck_path, catepath);
		break;
	case -1:
	case DECK_CATEGORY_NONE:
	case DECK_CATEGORY_SEPARATOR:
		myswprintf(catepath, L"./deck");
		break;
	default:
		myswprintf(catepath, L"./deck/%ls", text);
	}
	BufferIO::CopyWStr(catepath, ret, 256);
}

// 获取卡组文件的完整路径
void DeckManager::GetDeckFile(wchar_t* ret, int category_index, const wchar_t* category_name, const wchar_t* deckname) {
	wchar_t filepath[256];
	wchar_t catepath[256];
	if(deckname != nullptr) {
		GetCategoryPath(catepath, category_index, category_name);
		myswprintf(filepath, L"%ls/%ls.ydk", catepath, deckname);
		BufferIO::CopyWStr(filepath, ret, 256);
	}
	else {
		BufferIO::CopyWStr(L"", ret, 256);
	}
}

// 打开卡组文件
FILE* DeckManager::OpenDeckFile(const wchar_t* file, const char* mode) {
	FILE* fp = mywfopen(file, mode);
	return fp;
}

irr::io::IReadFile* DeckManager::OpenDeckReader(const wchar_t* file) {
#ifdef _WIN32
	auto reader = dataManager.FileSystem->createAndOpenFile(file);
#else
	char file2[256];
	BufferIO::EncodeUTF8(file, file2);
	auto reader = dataManager.FileSystem->createAndOpenFile(file2);
#endif
	return reader;
}

// 从输入流加载当前卡组
bool DeckManager::LoadCurrentDeck(std::istringstream& deckStream, bool is_packlist) {
	LoadDeckFromStream(current_deck, deckStream, is_packlist);
	return true;  // the above LoadDeck has return value but we ignore it here for now
}

// 从文件加载当前卡组
bool DeckManager::LoadCurrentDeck(const wchar_t* file, bool is_packlist) {
	current_deck.clear();
	if (!file[0])
		return false;
	char deckBuffer[MAX_YDK_SIZE]{};
	auto reader = OpenDeckReader(file);
	if(!reader) {
		wchar_t localfile[256];
		myswprintf(localfile, L"./deck/%ls.ydk", file);
		reader = OpenDeckReader(localfile);
	}
	if(!reader && !mywcsncasecmp(file, L"./pack", 6)) {
		wchar_t zipfile[256];
		myswprintf(zipfile, L"%ls", file + 2);
		reader = OpenDeckReader(zipfile);
	}
	if(!reader)
		return false;
	std::memset(deckBuffer, 0, sizeof deckBuffer);
	int size = reader->read(deckBuffer, sizeof deckBuffer);
	reader->drop();
	if (size >= (int)sizeof deckBuffer) {
		return false;
	}
	std::istringstream deckStream(deckBuffer);
	LoadDeckFromStream(current_deck, deckStream, is_packlist);
	return true;  // the above function has return value but we ignore it here for now
}

// 从分类和名称加载当前卡组
bool DeckManager::LoadCurrentDeck(int category_index, const wchar_t* category_name, const wchar_t* deckname) {
	wchar_t filepath[256];
	GetDeckFile(filepath, category_index, category_name, deckname);
	bool is_packlist = (category_index == DECK_CATEGORY_PACK);
	if(!LoadCurrentDeck(filepath, is_packlist))
		return false;
	if (mainGame->is_building)
		mainGame->deckBuilder.RefreshPackListScroll();
	return true;
}

// 将卡组保存到输出流
void DeckManager::SaveDeck(const Deck& deck, std::stringstream& deckStream) {
	deckStream << "#created by ..." << std::endl;
	deckStream << "#main" << std::endl;
	for(size_t i = 0; i < deck.main.size(); ++i)
		deckStream << deck.main[i]->first << std::endl;
	deckStream << "#extra" << std::endl;
	for(size_t i = 0; i < deck.extra.size(); ++i)
		deckStream << deck.extra[i]->first << std::endl;
	deckStream << "!side" << std::endl;
	for(size_t i = 0; i < deck.side.size(); ++i)
		deckStream << deck.side[i]->first << std::endl;
}

// 将卡组保存到文件
bool DeckManager::SaveDeck(const Deck& deck, const wchar_t* file) {
	if(!FileSystem::IsDirExists(L"./deck") && !FileSystem::MakeDir(L"./deck"))
		return false;
	FILE* fp = OpenDeckFile(file, "w");
	if(!fp)
		return false;
	std::stringstream deckStream;
	SaveDeck(deck, deckStream);
	std::fputs(deckStream.str().c_str(), fp);
	std::fclose(fp);
	return true;
}

// 删除卡组文件
bool DeckManager::DeleteDeck(const wchar_t* file) {
	return FileSystem::RemoveFile(file);
}

// 统计列表中某类卡牌的数量
int DeckManager::TypeCount(std::vector<code_pointer> list, unsigned int ctype) {
	int res = 0;
	for(size_t i = 0; i < list.size(); ++i) {
		code_pointer cur = list[i];
		if(cur->second.type & ctype)
			res++;
	}
	return res;
}

// 从Base64编码加载卡组
bool DeckManager::LoadDeckFromCode(Deck& deck, const unsigned char *code, int len) {
	unsigned char data[1024], *pdeck = data, *data_ = data;
	int decoded_len = Base64::DecodedLength(code, len);
	if(decoded_len > 1024 || decoded_len < 8 || !Base64::Decode(code, len, data_, decoded_len))
		return false;
	int mainc = BufferIO::Read<int32_t>(pdeck);
	int sidec = BufferIO::Read<int32_t>(pdeck);
	int errorcode = LoadDeck(deck, (uint32_t*)pdeck, mainc, sidec);
	return (errorcode == 0);
}

// 将卡组编码为Base64字符串
int DeckManager::SaveDeckToCode(Deck& deck, unsigned char* code) {
	unsigned char deckbuf[1024], *pdeck = deckbuf;
	BufferIO::Write<int32_t>(pdeck, deck.main.size() + deck.extra.size());
	BufferIO::Write<int32_t>(pdeck, deck.side.size());
	for(size_t i = 0; i < deck.main.size(); ++i)
		BufferIO::Write<int32_t>(pdeck, deck.main[i]->first);
	for(size_t i = 0; i < deck.extra.size(); ++i)
		BufferIO::Write<int32_t>(pdeck, deck.extra[i]->first);
	for(size_t i = 0; i < deck.side.size(); ++i)
		BufferIO::Write<int32_t>(pdeck, deck.side[i]->first);
	int len = pdeck - deckbuf;
	int encoded_len = Base64::EncodedLength(len);
	Base64::Encode(deckbuf, len, code, encoded_len+1);
	return encoded_len;
}

// 创建新的卡组分类（文件夹）
bool DeckManager::CreateCategory(const wchar_t* name) {
	if(!FileSystem::IsDirExists(L"./deck") && !FileSystem::MakeDir(L"./deck"))
		return false;
	if(name[0] == 0)
		return false;
	wchar_t localname[256];
	myswprintf(localname, L"./deck/%ls", name);
	return FileSystem::MakeDir(localname);
}

// 重命名卡组分类
bool DeckManager::RenameCategory(const wchar_t* oldname, const wchar_t* newname) {
	if(!FileSystem::IsDirExists(L"./deck") && !FileSystem::MakeDir(L"./deck"))
		return false;
	if(newname[0] == 0)
		return false;
	wchar_t oldlocalname[256];
	wchar_t newlocalname[256];
	myswprintf(oldlocalname, L"./deck/%ls", oldname);
	myswprintf(newlocalname, L"./deck/%ls", newname);
	return FileSystem::Rename(oldlocalname, newlocalname);
}

// 删除卡组分类
bool DeckManager::DeleteCategory(const wchar_t* name) {
	wchar_t localname[256];
	myswprintf(localname, L"./deck/%ls", name);
	if(!FileSystem::IsDirExists(localname))
		return false;
	return FileSystem::DeleteDir(localname);
}

// 保存DeckArray格式的卡组
bool DeckManager::SaveDeckArray(const DeckArray& deck, const wchar_t* name) {
	if (!FileSystem::IsDirExists(L"./deck") && !FileSystem::MakeDir(L"./deck"))
		return false;
	FILE* fp = OpenDeckFile(name, "w");
	if (!fp)
		return false;
	std::fprintf(fp, "#created by ...\n#main\n");
	for (const auto& code : deck.main)
		std::fprintf(fp, "%u\n", code);
	std::fprintf(fp, "#extra\n");
	for (const auto& code : deck.extra)
		std::fprintf(fp, "%u\n", code);
	std::fprintf(fp, "!side\n");
	for (const auto& code : deck.side)
		std::fprintf(fp, "%u\n", code);
	std::fclose(fp);
	return true;
}
#endif //YGOPRO_SERVER_MODE
}
