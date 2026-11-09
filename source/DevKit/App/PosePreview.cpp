#include "draw2d.h"

#include "Document.h"
#include "PosePreview.h"

namespace devkit {

PosePreview::~PosePreview () {
	for (std::map<std::string, std::shared_ptr<DevKitAnimBank> >::iterator i = banks.begin (); i != banks.end (); ++ i) {
		if (i->second && draw) draw->forgetPictures (& i->second->sprites ());
	}
}

AnimBankRef PosePreview::bank (const std::string & folder, const std::string & path) {
	if (path.empty () || folder.empty ()) return AnimBankRef ();
	std::string full = folder + path;
	std::map<std::string, std::shared_ptr<DevKitAnimBank> >::iterator found = banks.find (full);
	if (found != banks.end ()) return found->second;
	std::shared_ptr<DevKitAnimBank> loaded = std::make_shared<DevKitAnimBank> ();
	if (! loaded->load (full)) loaded.reset ();
	banks[full] = loaded;
	return loaded;
}

RigDefinitionRef PosePreview::rig (const animformat::Asset & asset, const std::string & folder) {
	std::shared_ptr<RigDefinition> r = std::make_shared<RigDefinition> (asset.rig);
	for (size_t i = 0; i < r->parts.size (); i ++) {
		SpriteRef & s = r->parts[i].sprite;
		if (s.kind == SpriteRef::SPRITE && s.bankFile >= 0 && (size_t) s.bankFile < asset.banks.size ()) s.bank = bank (folder, asset.banks[s.bankFile]);
	}
	return r;
}

ClipDefinitionRef PosePreview::clip (const animformat::Asset & asset, const std::string & folder) {
	std::shared_ptr<ClipDefinition> c = std::make_shared<ClipDefinition> (asset.clip);
	for (size_t t = 0; t < c->tracks.size (); t ++) {
		for (size_t k = 0; k < c->tracks[t].keys.size (); k ++) {
			SpriteRef & s = c->tracks[t].keys[k].sprite;
			if (s.kind == SpriteRef::SPRITE && s.bankFile >= 0 && (size_t) s.bankFile < asset.banks.size ()) s.bank = bank (folder, asset.banks[s.bankFile]);
		}
	}
	return c;
}

} // namespace devkit
