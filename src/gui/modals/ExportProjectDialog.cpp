/*
 * ExportProjectDialog.cpp - implementation of dialog for exporting project
 *
 * Copyright (c) 2004-2013 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "ExportProjectDialog.h"

#include <QMessageBox>
#include <QDir>

#include "Song.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "OutputSettings.h"

namespace lmms::gui
{

ExportProjectDialog::ExportProjectDialog( const QString & _file_name,
							QWidget * _parent, bool multi_export=false ) :
	QDialog( _parent ),
	Ui::ExportProjectDialog(),
	m_fileName( _file_name ),
	m_fileExtension(),
	m_multiExport( multi_export ),
	m_renderManager( nullptr )
{
	setupUi( this );

	m_targetFileInfo = QFileInfo(m_fileName);
	m_needCheckIfTargetFileExists = false;
	QString fileExt = m_targetFileInfo.suffix();
	m_originalTargetFileExt = "." + fileExt;
	
	updateWindowTitle();
	
	int cbIndex = 0;
	for( int i = 0; i < ProjectRenderer::NumFileFormats; ++i )
	{
		if( ProjectRenderer::fileEncodeDevices[i].isAvailable() )
		{
			// Get the extension of this format.
			QString renderExt = ProjectRenderer::fileEncodeDevices[i].m_extension;

			// Add to combo box.
			fileFormatCB->addItem( ProjectRenderer::tr(
				ProjectRenderer::fileEncodeDevices[i].m_description ),
				QVariant( static_cast<int>(ProjectRenderer::fileEncodeDevices[i].m_fileFormat) ) // Format tag; later used for identification.
			);

			// If this is our extension, select it.
			if( QString::compare( renderExt, m_originalTargetFileExt,
									Qt::CaseInsensitive ) == 0 )
			{
				fileFormatCB->setCurrentIndex( cbIndex );
			}

			cbIndex++;
		}
	}

	int const MAX_LEVEL=8;
	for(int i=0; i<=MAX_LEVEL; ++i)
	{
		QString info="";
		if ( i==0 ){ info = tr( "( Fastest - biggest )" ); }
		else if ( i==MAX_LEVEL ){ info = tr( "( Slowest - smallest )" ); }

		compLevelCB->addItem(
			QString::number(i)+" "+info,
			QVariant(i/static_cast<double>(MAX_LEVEL))
		);
	}
	compLevelCB->setCurrentIndex(5);
#ifndef LMMS_HAVE_SF_COMPLEVEL
	// Disable this widget; the setting would be ignored by the renderer.
	compressionWidget->setVisible(false);
#endif

	connect( startButton, SIGNAL(clicked()),
			this, SLOT(startBtnClicked()));
}

void ExportProjectDialog::updateWindowTitle()
{
	setWindowTitle( tr( "Export project to %1" ).arg(
					m_targetFileInfo.fileName() ) );
}

QString ExportProjectDialog::removeKnownExtension(QString filePath)
{
	QFileInfo info(filePath);

	QString fileExt = info.suffix();
	
	if (fileExt.compare("") == 0) return filePath;
	
	fileExt = "." + fileExt;

	for(int i = 0; i < ProjectRenderer::NumFileFormats; i++)
	{
		if( ProjectRenderer::fileEncodeDevices[i].isAvailable() )
		{
			if (fileExt.compare(ProjectRenderer::fileEncodeDevices[i].m_extension, Qt::CaseInsensitive ) == 0) 
				return QDir(info.absolutePath()).filePath(info.completeBaseName());
		} 
	}

	return info.absoluteFilePath();
}

void ExportProjectDialog::reject()
{
	if( m_renderManager ) {
		m_renderManager->abortProcessing();
	}
	m_renderManager.reset(nullptr);

	QDialog::reject();
}



void ExportProjectDialog::accept()
{
	m_renderManager.reset(nullptr);
	QDialog::accept();

	getGUI()->mainWindow()->resetWindowTitle();
}




void ExportProjectDialog::closeEvent( QCloseEvent * _ce )
{
	Engine::getSong()->setLoopRenderCount(1);
	if( m_renderManager ) {
		m_renderManager->abortProcessing();
	}

	QDialog::closeEvent( _ce );
}


OutputSettings::StereoMode mapToStereoMode(int index)
{
	switch (index)
	{
	case 0:
		return OutputSettings::StereoMode::Mono;
	case 1:
		return OutputSettings::StereoMode::Stereo;
	case 2:
		return OutputSettings::StereoMode::JointStereo;
	default:
		return OutputSettings::StereoMode::Stereo;
	}
}

void ExportProjectDialog::startExport()
{
	AudioEngine::qualitySettings qs =
			AudioEngine::qualitySettings(
					static_cast<AudioEngine::qualitySettings::Interpolation>(interpolationCB->currentIndex()),
					static_cast<AudioEngine::qualitySettings::Oversampling>(oversamplingCB->currentIndex()) );

	const auto samplerates = std::array{44100, 48000, 88200, 96000, 192000};
	const auto bitrates = std::array{64, 128, 160, 192, 256, 320};

	bool useVariableBitRate = checkBoxVariableBitRate->isChecked();

	OutputSettings::BitRateSettings bitRateSettings(bitrates[ bitrateCB->currentIndex() ], useVariableBitRate);
	OutputSettings os = OutputSettings(
			samplerates[ samplerateCB->currentIndex() ],
			bitRateSettings,
			static_cast<OutputSettings::BitDepth>( depthCB->currentIndex() ),
			mapToStereoMode(stereoModeComboBox->currentIndex()) );

	if (compressionWidget->isVisible())
	{
		double level = compLevelCB->itemData(compLevelCB->currentIndex()).toDouble();
		os.setCompressionLevel(level);
	}

	// Make sure we have the the correct file extension
	// so there's no confusion about the codec in use.
	auto output_name = m_fileName;
	if (!(m_multiExport || output_name.endsWith(m_fileExtension,Qt::CaseInsensitive)))
	{
		output_name+=m_fileExtension;
	}
	m_renderManager.reset(new RenderManager( qs, os, m_ft, output_name ));

	Engine::getSong()->setExportLoop( exportLoopCB->isChecked() );
	Engine::getSong()->setRenderBetweenMarkers( renderMarkersCB->isChecked() );
	Engine::getSong()->setLoopRenderCount(loopCountSB->value());

	connect( m_renderManager.get(), SIGNAL(progressChanged(int)),
			progressBar, SLOT(setValue(int)));
	connect( m_renderManager.get(), SIGNAL(progressChanged(int)),
			this, SLOT(updateTitleBar(int)));
	connect( m_renderManager.get(), SIGNAL(finished()),
			this, SLOT(accept())) ;
	connect( m_renderManager.get(), SIGNAL(finished()),
			getGUI()->mainWindow(), SLOT(resetWindowTitle()));

	if ( m_multiExport )
	{
		m_renderManager->renderTracks();
	}
	else
	{
		m_renderManager->renderProject();
	}
}


void ExportProjectDialog::onFileFormatChanged(int index)
{
	// Extract the format tag from the currently selected item,
	// and adjust the UI properly.
	QVariant format_tag = fileFormatCB->itemData(index);
	bool successful_conversion = false;
	auto exportFormat = static_cast<ProjectRenderer::ExportFileFormat>(
		format_tag.toInt(&successful_conversion)
	);
	Q_ASSERT(successful_conversion);

	bool stereoModeVisible = (exportFormat == ProjectRenderer::ExportFileFormat::MP3);

	bool sampleRateControlsVisible = (exportFormat != ProjectRenderer::ExportFileFormat::MP3);

	bool bitRateControlsEnabled =
			(exportFormat == ProjectRenderer::ExportFileFormat::Ogg ||
			 exportFormat == ProjectRenderer::ExportFileFormat::MP3);

	bool bitDepthControlEnabled =
			(exportFormat == ProjectRenderer::ExportFileFormat::Wave ||
			 exportFormat == ProjectRenderer::ExportFileFormat::Flac);

	bool variableBitrateVisible = !(exportFormat == ProjectRenderer::ExportFileFormat::MP3 || exportFormat == ProjectRenderer::ExportFileFormat::Flac);

#ifdef LMMS_HAVE_SF_COMPLEVEL
	bool compressionLevelVisible = (exportFormat == ProjectRenderer::ExportFileFormat::Flac);
	compressionWidget->setVisible(compressionLevelVisible);
#endif

	stereoModeWidget->setVisible(stereoModeVisible);
	sampleRateWidget->setVisible(sampleRateControlsVisible);

	bitrateWidget->setVisible(bitRateControlsEnabled);
	checkBoxVariableBitRate->setVisible(variableBitrateVisible);

	depthWidget->setVisible(bitDepthControlEnabled);
		
	// Updating target file extention and checking status

	m_targetFileInfo.setFile(removeKnownExtension(m_targetFileInfo.absoluteFilePath()) 
							+ ProjectRenderer::fileEncodeDevices[index].m_extension);
	
	m_needCheckIfTargetFileExists = m_originalTargetFileExt.compare(ProjectRenderer::fileEncodeDevices[index].m_extension, 
																	Qt::CaseInsensitive ) != 0;

	updateWindowTitle();
}

void ExportProjectDialog::startBtnClicked()
{
	if (m_needCheckIfTargetFileExists && m_targetFileInfo.exists())
	{
		QMessageBox mb;
		mb.setWindowTitle(windowTitle());
		mb.setText(m_targetFileInfo.fileName() + tr(" already exists. Do you want to replace it?"));
		mb.setIcon(QMessageBox::Warning);
		mb.setStandardButtons(QMessageBox::Yes | QMessageBox::No);

		if(mb.exec() == QMessageBox::No) return;
	}

	m_ft = ProjectRenderer::ExportFileFormat::Count;

	// Get file format from current menu selection.
	bool successful_conversion = false;
	QVariant tag = fileFormatCB->itemData(fileFormatCB->currentIndex());
	m_ft = static_cast<ProjectRenderer::ExportFileFormat>(
			tag.toInt(&successful_conversion)
	);

	if( !successful_conversion )
	{
		QMessageBox::information( this, tr( "Error" ),
								  tr( "Error while determining file-encoder device. "
									  "Please try to choose a different output "
									  "format." ) );
		reject();
		return;
	}

	// Find proper file extension.
	for( int i = 0; i < ProjectRenderer::NumFileFormats; ++i )
	{
		if (m_ft == ProjectRenderer::fileEncodeDevices[i].m_fileFormat)
		{
			m_fileExtension = QString( QLatin1String( ProjectRenderer::fileEncodeDevices[i].m_extension ) );
			break;
		}
	}

	startButton->setEnabled( false );
	progressBar->setEnabled( true );

	updateTitleBar( 0 );

	startExport();
}




void ExportProjectDialog::updateTitleBar( int _prog )
{
	getGUI()->mainWindow()->setWindowTitle(
					tr( "Rendering: %1%" ).arg( _prog ) );
}

} // namespace lmms::gui
