#include "data_handler.h"
#include <fstream>
#include <curl/curl.h>
#include <irrlicht.h>
#include "utils_gui.h"
#include "deck_manager.h"
#include "logging.h"
#include "utils.h"
#ifndef __ANDROID__
#include "IrrlichtCommonIncludes/CFileSystem.h"
#else
#include "Android/COSAndroidOperator.h"
#include "IrrlichtCommonIncludes1.9/CFileSystem.h"
#include "Android/porting_android.h"
#endif

namespace ygo {

void DataHandler::LoadDatabases() {
	if(std::ifstream("cards.cdb").good())
		dataManager->LoadDB(EPRO_TEXT("cards.cdb"));
	for(auto& file : Utils::FindFiles(EPRO_TEXT("./expansions/"), { EPRO_TEXT("cdb") }, 2))
		dataManager->LoadDB(EPRO_TEXT("./expansions/") + file);
	LoadArchivesDB();
}
void DataHandler::LoadArchivesDB() {
	std::vector<char> buffer;
	for(auto& archive : Utils::archives) {
		std::lock_guard<std::mutex> guard(*archive.mutex);
		auto files = Utils::FindFiles(archive.archive, EPRO_TEXT(""), { EPRO_TEXT("cdb") }, 3);
		for(auto& index : files) {
			auto reader = archive.archive->createAndOpenFile(index);
			if(reader == nullptr)
				continue;
			buffer.resize(reader->getSize());
			reader->read(buffer.data(), buffer.size());
			std::string filename(irr::core::stringc(reader->getFileName()).c_str()); //the zip loader stores the names as utf8
			reader->drop();
			dataManager->LoadDBFromBuffer(buffer, filename);
		}
	}
}

void DataHandler::LoadPicUrls() {
	for(auto& _config : { &configs->user_configs, &configs->configs }) {
		auto& config = *_config;
		auto it = config.find("urls");
		if(it != config.end() && it->is_array()) {
			for(auto& obj : *it) {
				try {
					const auto& type = obj.at("type").get_ref<std::string&>();
					const auto& url = obj.at("url").get_ref<std::string&>();
					if(url == "default") {
						if(type == "pic") {
#ifdef DEFAULT_PIC_URL
							imageDownloader->AddDownloadResource({ DEFAULT_PIC_URL, imgType::ART });
#else
							continue;
#endif
						} else if(type == "field") {
#ifdef DEFAULT_FIELD_URL
							imageDownloader->AddDownloadResource({ DEFAULT_FIELD_URL, imgType::FIELD });
#else
							continue;
#endif
						} else if(type == "cover") {
#ifdef DEFAULT_COVER_URL
							imageDownloader->AddDownloadResource({ DEFAULT_COVER_URL, imgType::COVER });
#else
							continue;
#endif
						}
					} else {
						imageDownloader->AddDownloadResource({ url, type == "field" ?
																imgType::FIELD : (type == "pic") ?
																imgType::ART : imgType::COVER });
					}
				}
				catch(std::exception& e) {
					ErrorLog(fmt::format("Exception occurred: {}", e.what()));
				}
			}
		}
	}
}
void DataHandler::LoadZipArchives() {
	irr::io::IFileArchive* tmp_archive = nullptr;
	for(auto& file : Utils::FindFiles(EPRO_TEXT("./expansions/"), { EPRO_TEXT("zip") })) {
		filesystem->addFileArchive(fmt::format(EPRO_TEXT("./expansions/{}"), file).data(), true, false, irr::io::EFAT_ZIP, "", &tmp_archive);
		if(tmp_archive) {
			Utils::archives.emplace_back(tmp_archive);
		}
	}
}
DataHandler::DataHandler(epro::path_stringview working_dir) {
	configs = std::unique_ptr<GameConfig>(new GameConfig);
	gGameConfig = configs.get();
	tmp_device = nullptr;
#ifndef __ANDROID__
	tmp_device = GUIUtils::CreateDevice(configs.get());
	Utils::OSOperator = tmp_device->getGUIEnvironment()->getOSOperator();
	Utils::OSOperator->grab();
#else
	Utils::OSOperator = new irr::COSAndroidOperator();
	configs->ssl_certificate_path = fmt::format("{}/cacert.cer", porting::internal_storage);
#endif
	filesystem = new irr::io::CFileSystem();
	Utils::filesystem = filesystem;
	Utils::working_dir = { working_dir.data(), working_dir.size() };
	LoadZipArchives();
	deckManager = std::unique_ptr<DeckManager>(new DeckManager());
	gitManager = std::unique_ptr<RepoManager>(new RepoManager());
	sounds = std::unique_ptr<SoundManager>(new SoundManager(configs->soundVolume / 100.0, configs->musicVolume / 100.0, configs->enablesound, configs->enablemusic, working_dir));
	gitManager->LoadRepositoriesFromJson(configs->user_configs);
	gitManager->LoadRepositoriesFromJson(configs->configs);
	dataManager = std::unique_ptr<DataManager>(new DataManager());
	imageDownloader = std::unique_ptr<ImageDownloader>(new ImageDownloader());
	LoadDatabases();
	LoadPicUrls();
	deckManager->LoadLFList();
	auto strings_loaded = dataManager->LoadStrings(EPRO_TEXT("./config/strings.conf"));
	strings_loaded = dataManager->LoadStrings(EPRO_TEXT("./expansions/strings.conf")) || strings_loaded;
	if(!strings_loaded) {
		throw std::runtime_error("Failed to load strings!");
	}
}
DataHandler::~DataHandler() {
	if(filesystem)
		filesystem->drop();
	if(Utils::OSOperator)
		Utils::OSOperator->drop();
}

}
