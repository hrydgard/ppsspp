#include "qkeyedit.h"
#include <QKeySequence>
#include <QKeyEvent>

QKeyEdit::QKeyEdit(QWidget *parent) :
	QLineEdit(parent)
{
}

bool QKeyEdit::event(QEvent *e)
{
	if(e->type() == QEvent::KeyPress)
	{
		QKeyEvent *ke = static_cast<QKeyEvent *>(e);
		QKeySequence seq(ke->key());
		setText(seq.toString());
		return true;
	}

	return QLineEdit::event(e);
}
