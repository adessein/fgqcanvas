#ifndef FGQCANVASIMAGE_H
#define FGQCANVASIMAGE_H

#include <QPixmap>

#include "fgcanvaselement.h"

class FGQCanvasImage : public FGCanvasElement
{
public:
    FGQCanvasImage(FGCanvasGroup* pr, LocalProp* prop);

protected:
    virtual void doPaint(FGCanvasPaintContext* context) const override;

    virtual void markStyleDirty() override;

private:
    bool onChildAdded(LocalProp *prop) override;

    void rebuildImage() const;
private:
    mutable bool _imageDirty;
    mutable QPixmap _image;
    QString _source;
};

#endif // FGQCANVASIMAGE_H
