/*
 * Copyright (C) 2015  University of Oregon
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Apache License, as specified in the LICENSE file.
 *
 * For more information, see the LICENSE file.
 */

package  vnmr.util;

import java.awt.*;
import javax.swing.*;

public interface VSplitListener {
    public void setSplitSize(VSeparator comp, int size);
    public void setSplitPos(VSeparator comp, int  pos);
}
