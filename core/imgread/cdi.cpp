#include "common.h"
#include "stdclass.h"
#include "oslib/storage.h"

#include "deps/chdpsr/cdipsr.h"

Disc* cdi_parse(const char* file, std::vector<u8> *digest)
{
	if (get_file_extension(file) != "cdi")
		return nullptr;

	FILE *fsource = hostfs::storage().openFile(file, "rb");

	if (fsource == nullptr)
	{
		WARN_LOG(GDROM, "Cannot open file '%s' errno %d", file, errno);
		throw FlycastException(std::string("Cannot open CDI file ") + file);
	}
	try {
		image_s image = { 0 };
		track_s track = { 0 };
		if (!CDI_init(fsource, &image, file))
			throw FlycastException(std::string("Invalid CDI file ") + file);
	
		if (!CDI_get_sessions(fsource, &image)) {
			WARN_LOG(GDROM, "Invalid CDI file '%s': can't get sessions", file);
			throw FlycastException("Invalid CDI image");
		}
		if (image.sessions > 255) {
			WARN_LOG(GDROM, "Invalid CDI file '%s': too many sessions (%d)", file, image.sessions);
			throw FlycastException("Invalid CDI image");
		}

		std::unique_ptr<Disc> rv = std::make_unique<Disc>();

		image.remaining_sessions = image.sessions;

		/////////////////////////////////////////////////////////////// Loop sessions

		bool ft=true, CD_M2=false,CD_M1=false,CD_DA=false;

		while(image.remaining_sessions > 0)
		{
			ft=true;
			image.global_current_session++;

			if (!CDI_get_tracks(fsource, &image)) {
				WARN_LOG(GDROM, "Invalid CDI file '%s': can't get tracks", file);
				throw FlycastException("Invalid CDI image");
			}

			image.header_position = std::ftell(fsource);

			if (image.tracks == 0)
				INFO_LOG(GDROM, "Open session");
			else
			{
				if (image.tracks > 100) {
					WARN_LOG(GDROM, "Invalid CDI file '%s': too many tracks (%d)", file, image.tracks);
					throw FlycastException("Invalid CDI image");
				}
				// Clear cuesheet
				image.remaining_tracks = image.tracks;

				///////////////////////////////////////////////////////////////// Loop tracks

				while(image.remaining_tracks > 0)
				{
					track.global_current_track++;
					track.number = image.tracks - image.remaining_tracks + 1;

					if (!CDI_read_track (fsource, &image, &track))
						throw FlycastException("Invalid CDI image");

					if (track.sector_size != 2048 && track.sector_size != 2336 && track.sector_size != 2352 && track.sector_size != 2448)
					{
						WARN_LOG(GDROM, "Invalid sector size: %lu", track.sector_size);
						throw FlycastException("Invalid CDI sector size");
					}

					image.header_position = std::ftell(fsource);

					// Show info
#if 0
					printf("Saving  ");
					printf("Track: %2d  ",track.global_current_track);
					printf("Type: ");
					switch(track.mode)
					{
					case 0 : printf("Audio/"); break;
					case 1 : printf("Mode1/"); break;
					case 2 :
					default: printf("Mode2/"); break;
					}
					printf("%lu  ",track.sector_size);

					printf("Pregap: %-3ld  ",track.pregap_length);
					printf("Size: %-6ld  ",track.length);
					printf("LBA: %-6ld  ",track.start_lba);
#endif
					if (ft)
					{
						ft = false;
						Session s;
						s.StartFAD = track.pregap_length + track.start_lba;
						s.FirstTrack = (u8)track.global_current_track;
						rv->sessions.push_back(s);
					}

					Track t;
					if (track.mode==2)
						CD_M2=true;
					if (track.mode==1)
						CD_M1=true;
					if (track.mode==0)
						CD_DA=true;

					t.ADR=1;//hmm is that ok ?

					t.CTRL=track.mode==0?0:4;
					t.StartFAD=track.start_lba+track.pregap_length;
					t.EndFAD=t.StartFAD+track.length-1;
					FILE *trackFile = hostfs::storage().openFile(file, "rb");
					if (trackFile == nullptr) {
						WARN_LOG(GDROM, "Cannot re-open file '%s' errno %d", file, errno);
						throw FlycastException("Cannot re-open CDI file");
					}
					t.file = new RawTrackFile(trackFile, track.position + track.pregap_length * track.sector_size, t.StartFAD, track.sector_size);

					rv->tracks.push_back(t);

					if (track.length < 0)
						WARN_LOG(GDROM, "Negative track size found. You must extract image with /pregap option");

					std::fseek(fsource, track.position, SEEK_SET);
					if (track.total_length < track.length + track.pregap_length)
					{
						WARN_LOG(GDROM, "This track seems truncated. Skipping...");
						// FIXME that can't be right
						std::fseek(fsource, track.total_length, SEEK_CUR);
					}
					else
					{
						std::fseek(fsource, track.total_length * track.sector_size, SEEK_CUR);
						rv->EndFAD = track.start_lba + track.total_length - 1;
					}
					track.position = std::ftell(fsource);

					std::fseek(fsource, image.header_position, SEEK_SET);

					image.remaining_tracks--;
				}
			}

			CDI_skip_next_session(fsource, &image);

			image.remaining_sessions--;
		}
		if (digest != nullptr)
			*digest = MD5Sum().add(fsource).getDigest();
		std::fclose(fsource);

		rv->type=GuessDiscType(CD_M1,CD_M2,CD_DA);

		rv->LeadOut.StartFAD = rv->EndFAD;
		rv->LeadOut.ADR = 1;
		rv->LeadOut.CTRL = 4;

		return rv.release();
	} catch (...) {
		std::fclose(fsource);
		throw;
	}
}

