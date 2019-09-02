/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2015 - ROLI Ltd.

   Permission is granted to use this software under the terms of either:
   a) the GPL v2 (or any later version)
   b) the Affero GPL v3

   Details of these licenses can be found at: www.gnu.org/licenses

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

   ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.juce.com for more information.

  ==============================================================================
*/

#include "singlelinetexteditor.h"

// a word or space that can't be broken down any further
struct TextAtom
{
    //==============================================================================
    String atomText;
    float width;
    int numChars;

    //==============================================================================
    bool isWhitespace() const noexcept       { return CharacterFunctions::isWhitespace (atomText[0]); }
    bool isNewLine() const noexcept          { return atomText[0] == '\r' || atomText[0] == '\n'; }

    String getText () const
    {
        return atomText;
    }

    String getTrimmedText () const
    {
        return atomText.substring (0, numChars);
    }

    JUCE_LEAK_DETECTOR (TextAtom)
};

//==============================================================================
// a run of text with a single font and colour
class SingleLineTextEditor::UniformTextSection
{
public:
    UniformTextSection (const String& text, const Font& f, Colour col)
        : font (f), colour (col)
    {
        initialiseAtoms (text);
    }

    UniformTextSection (const UniformTextSection& other)
        : font (other.font), colour (other.colour)
    {
        atoms.addCopiesOf (other.atoms);
    }

    void append (UniformTextSection& other)
    {
        if (other.atoms.size() > 0)
        {
            int i = 0;

            if (TextAtom* const lastAtom = atoms.getLast())
            {
                if (! CharacterFunctions::isWhitespace (lastAtom->atomText.getLastCharacter()))
                {
                    TextAtom* const first = other.atoms.getUnchecked(0);

                    if (! CharacterFunctions::isWhitespace (first->atomText[0]))
                    {
                        lastAtom->atomText += first->atomText;
                        lastAtom->numChars = (uint16) (lastAtom->numChars + first->numChars);
                        lastAtom->width = font.getStringWidthFloat (lastAtom->getText());
                        delete first;
                        ++i;
                    }
                }
            }

            atoms.ensureStorageAllocated (atoms.size() + other.atoms.size() - i);

            while (i < other.atoms.size())
            {
                atoms.add (other.atoms.getUnchecked(i));
                ++i;
            }

            other.atoms.clear (false);
        }
    }

    UniformTextSection* split (const int indexToBreakAt)
    {
        UniformTextSection* const section2 = new UniformTextSection (String(), font, colour);
        int index = 0;

        for (int i = 0; i < atoms.size(); ++i)
        {
            TextAtom* const atom = atoms.getUnchecked(i);

            const int nextIndex = index + atom->numChars;

            if (index == indexToBreakAt)
            {
                for (int j = i; j < atoms.size(); ++j)
                    section2->atoms.add (atoms.getUnchecked (j));

                atoms.removeRange (i, atoms.size(), false);
                break;
            }
            else if (indexToBreakAt >= index && indexToBreakAt < nextIndex)
            {
                TextAtom* const secondAtom = new TextAtom();

                secondAtom->atomText = atom->atomText.substring (indexToBreakAt - index);
                secondAtom->width = font.getStringWidthFloat (secondAtom->getText());
                secondAtom->numChars = (uint16) secondAtom->atomText.length();

                section2->atoms.add (secondAtom);

                atom->atomText = atom->atomText.substring (0, indexToBreakAt - index);
                atom->width = font.getStringWidthFloat (atom->getText());
                atom->numChars = (uint16) (indexToBreakAt - index);

                for (int j = i + 1; j < atoms.size(); ++j)
                    section2->atoms.add (atoms.getUnchecked (j));

                atoms.removeRange (i + 1, atoms.size(), false);
                break;
            }

            index = nextIndex;
        }

        return section2;
    }

    void appendAllText (MemoryOutputStream& mo) const
    {
        for (int i = 0; i < atoms.size(); ++i)
            mo << atoms.getUnchecked(i)->atomText;
    }

    void appendSubstring (MemoryOutputStream& mo, const Range<int> range) const
    {
        int index = 0;
        for (int i = 0; i < atoms.size(); ++i)
        {
            const TextAtom* const atom = atoms.getUnchecked (i);
            const int nextIndex = index + atom->numChars;

            if (range.getStart() < nextIndex)
            {
                if (range.getEnd() <= index)
                    break;

                const Range<int> r ((range - index).getIntersectionWith (Range<int> (0, (int) atom->numChars)));

                if (! r.isEmpty())
                    mo << atom->atomText.substring (r.getStart(), r.getEnd());
            }

            index = nextIndex;
        }
    }

    int getTotalLength() const noexcept
    {
        int total = 0;

        for (int i = atoms.size(); --i >= 0;)
            total += atoms.getUnchecked(i)->numChars;

        return total;
    }

    void setFont (const Font& newFont)
    {
        if (font != newFont)
        {
            font = newFont;

            for (int i = atoms.size(); --i >= 0;)
            {
                TextAtom* const atom = atoms.getUnchecked(i);
                atom->width = newFont.getStringWidthFloat (atom->getText());
            }
        }
    }

    //==============================================================================
    Font font;
    Colour colour;
    OwnedArray<TextAtom> atoms;

private:
    void initialiseAtoms (const String& textToParse)
    {
        String::CharPointerType text (textToParse.getCharPointer());

        while (! text.isEmpty())
        {
            size_t numChars = 0;
            String::CharPointerType start (text);

            // create a whitespace atom unless it starts with non-ws
            if (text.isWhitespace() && *text != '\r' && *text != '\n')
            {
                do
                {
                    ++text;
                    ++numChars;
                }
                while (text.isWhitespace() && *text != '\r' && *text != '\n');
            }
            else
            {
                if (*text == '\r')
                {
                    ++text;
                    ++numChars;

                    if (*text == '\n')
                    {
                        ++start;
                        ++text;
                    }
                }
                else if (*text == '\n')
                {
                    ++text;
                    ++numChars;
                }
                else
                {
                    while (! (text.isEmpty() || text.isWhitespace()))
                    {
                        ++text;
                        ++numChars;
                    }
                }
            }

            TextAtom* const atom = atoms.add (new TextAtom());

            atom->atomText = String (start, numChars);
            atom->width = font.getStringWidthFloat (atom->getText());
            atom->numChars = (uint16) numChars;
        }
    }

    UniformTextSection& operator= (const UniformTextSection&);
    JUCE_LEAK_DETECTOR (UniformTextSection)
};

//==============================================================================
class SingleLineTextEditor::Iterator
{
public:
    Iterator (const OwnedArray<UniformTextSection>& sectionList,
              const float wrapWidth, Justification j)
      : indexInText (0),
        lineY (0),
        lineHeight (0),
        maxDescent (0),
        atomX (0),
        atomRight (0),
        atom (nullptr),
        currentSection (nullptr),
        justification (j),
        sections (sectionList),
        sectionIndex (0),
        atomIndex (0),
        wordWrapWidth (wrapWidth)
    {
        jassert (wordWrapWidth > 0);

        if (sections.size() > 0)
        {
            currentSection = sections.getUnchecked (sectionIndex);

            if (currentSection != nullptr)
                beginNewLine();
        }
    }

    Iterator (const Iterator& other)
      : indexInText (other.indexInText),
        lineY (other.lineY),
        lineHeight (other.lineHeight),
        maxDescent (other.maxDescent),
        atomX (other.atomX),
        atomRight (other.atomRight),
        atom (other.atom),
        currentSection (other.currentSection),
        justification (other.justification),
        sections (other.sections),
        sectionIndex (other.sectionIndex),
        atomIndex (other.atomIndex),
        wordWrapWidth (other.wordWrapWidth),
        tempAtom (other.tempAtom)
    {
    }

    //==============================================================================
    bool next()
    {
        if (atom == &tempAtom)
        {
            const int numRemaining = tempAtom.atomText.length() - tempAtom.numChars;

            if (numRemaining > 0)
            {
                tempAtom.atomText = tempAtom.atomText.substring (tempAtom.numChars);

                atomX = 0;

                if (tempAtom.numChars > 0)
                    lineY += lineHeight;

                indexInText += tempAtom.numChars;

                GlyphArrangement g;
                g.addLineOfText (currentSection->font, atom->getText(), 0.0f, 0.0f);

                int split;
                for (split = 0; split < g.getNumGlyphs(); ++split)
                    if (shouldWrap (g.getGlyph (split).getRight()))
                        break;

                if (split > 0 && split <= numRemaining)
                {
                    tempAtom.numChars = (uint16) split;
                    tempAtom.width = g.getGlyph (split - 1).getRight();
                    atomRight = atomX + tempAtom.width;
                    return true;
                }
            }
        }

        bool forceNewLine = false;

        if (sectionIndex >= sections.size())
        {
            moveToEndOfLastAtom();
            return false;
        }
        else if (atomIndex >= currentSection->atoms.size() - 1)
        {
            if (atomIndex >= currentSection->atoms.size())
            {
                if (++sectionIndex >= sections.size())
                {
                    moveToEndOfLastAtom();
                    return false;
                }

                atomIndex = 0;
                currentSection = sections.getUnchecked (sectionIndex);
            }
            else
            {
                const TextAtom* const lastAtom = currentSection->atoms.getUnchecked (atomIndex);

                if (! lastAtom->isWhitespace())
                {
                    // handle the case where the last atom in a section is actually part of the same
                    // word as the first atom of the next section...
                    float right = atomRight + lastAtom->width;
                    float lineHeight2 = lineHeight;
                    float maxDescent2 = maxDescent;

                    for (int section = sectionIndex + 1; section < sections.size(); ++section)
                    {
                        const UniformTextSection* const s = sections.getUnchecked (section);

                        if (s->atoms.size() == 0)
                            break;

                        const TextAtom* const nextAtom = s->atoms.getUnchecked (0);

                        if (nextAtom->isWhitespace())
                            break;

                        right += nextAtom->width;

                        lineHeight2 = jmax (lineHeight2, s->font.getHeight());
                        maxDescent2 = jmax (maxDescent2, s->font.getDescent());

                        if (shouldWrap (right))
                        {
                            lineHeight = lineHeight2;
                            maxDescent = maxDescent2;

                            forceNewLine = true;
                            break;
                        }

                        if (s->atoms.size() > 1)
                            break;
                    }
                }
            }
        }

        if (atom != nullptr)
        {
            atomX = atomRight;
            indexInText += atom->numChars;

            if (atom->isNewLine())
                beginNewLine();
        }

        atom = currentSection->atoms.getUnchecked (atomIndex);
        atomRight = atomX + atom->width;
        ++atomIndex;

        if (shouldWrap (atomRight) || forceNewLine)
        {
            if (atom->isWhitespace())
            {
                // leave whitespace at the end of a line, but truncate it to avoid scrolling
                atomRight = jmin (atomRight, wordWrapWidth);
            }
            else
            {
                atomRight = atom->width;

                if (shouldWrap (atomRight))  // atom too big to fit on a line, so break it up..
                {
                    tempAtom = *atom;
                    tempAtom.width = 0;
                    tempAtom.numChars = 0;
                    atom = &tempAtom;

                    if (atomX > 0)
                        beginNewLine();

                    return next();
                }

                beginNewLine();
                return true;
            }
        }

        return true;
    }

    void beginNewLine()
    {
        atomX = 0;
        lineY += lineHeight;

        int tempSectionIndex = sectionIndex;
        int tempAtomIndex = atomIndex;
        const UniformTextSection* section = sections.getUnchecked (tempSectionIndex);

        lineHeight = section->font.getHeight();
        maxDescent = section->font.getDescent();

        float x = (atom != nullptr) ? atom->width : 0;

        while (! shouldWrap (x))
        {
            if (tempSectionIndex >= sections.size())
                break;

            bool checkSize = false;

            if (tempAtomIndex >= section->atoms.size())
            {
                if (++tempSectionIndex >= sections.size())
                    break;

                tempAtomIndex = 0;
                section = sections.getUnchecked (tempSectionIndex);
                checkSize = true;
            }

            const TextAtom* const nextAtom = section->atoms.getUnchecked (tempAtomIndex);

            if (nextAtom == nullptr)
                break;

            x += nextAtom->width;

            if (shouldWrap (x) || nextAtom->isNewLine())
                break;

            if (checkSize)
            {
                lineHeight = jmax (lineHeight, section->font.getHeight());
                maxDescent = jmax (maxDescent, section->font.getDescent());
            }

            ++tempAtomIndex;
        }
    }

    //==============================================================================
    void draw (Graphics& g, const UniformTextSection*& lastSection) const
    {
        if (! atom->isWhitespace())
        {
            if (lastSection != currentSection)
            {
                lastSection = currentSection;
                g.setColour (currentSection->colour);
                g.setFont (currentSection->font);
            }

            jassert (atom->getTrimmedText().isNotEmpty());

            GlyphArrangement ga;
            ga.addJustifiedText (currentSection->font, atom->getTrimmedText(),
                                 atomX, lineY + lineHeight - maxDescent, 100.0f, justification);

            ga.draw (g);
        }
    }

    void addSelection (RectangleList<float>& area, const Range<int> selected) const
    {
        const float startX = indexToX (selected.getStart());
        const float endX   = indexToX (selected.getEnd());

        area.add (startX, lineY, endX - startX, lineHeight);
    }

    void drawUnderline (Graphics& g, const Range<int> underline, const Colour colour) const
    {
        const int startX    = roundToInt (indexToX (underline.getStart()));
        const int endX      = roundToInt (indexToX (underline.getEnd()));
        const int baselineY = roundToInt (lineY + currentSection->font.getAscent() + 0.5f);

        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (Rectangle<int> (startX, baselineY, endX - startX, 1));
        g.fillCheckerBoard (Rectangle<int> (endX, baselineY + 1).toFloat(), 3, 1, colour, Colours::transparentBlack);
    }

    void drawSelectedText (Graphics& g,
                           const Range<int> selected,
                           const Colour selectedTextColour) const
    {
        if (! atom->isWhitespace())
        {
            GlyphArrangement ga;
            ga.addLineOfText (currentSection->font,
                              atom->getTrimmedText(),
                              atomX, (float) roundToInt (lineY + lineHeight - maxDescent));

            if (selected.getEnd() < indexInText + atom->numChars)
            {
                GlyphArrangement ga2 (ga);
                ga2.removeRangeOfGlyphs (0, selected.getEnd() - indexInText);
                ga.removeRangeOfGlyphs (selected.getEnd() - indexInText, -1);

                g.setColour (currentSection->colour);
                ga2.draw (g);
            }

            if (selected.getStart() > indexInText)
            {
                GlyphArrangement ga2 (ga);
                ga2.removeRangeOfGlyphs (selected.getStart() - indexInText, -1);
                ga.removeRangeOfGlyphs (0, selected.getStart() - indexInText);

                g.setColour (currentSection->colour);
                ga2.draw (g);
            }

            g.setColour (selectedTextColour);
            ga.draw (g);
        }
    }

    //==============================================================================
    float indexToX (const int indexToFind) const
    {
        if (indexToFind <= indexInText)
            return atomX;

        if (indexToFind >= indexInText + atom->numChars)
            return atomRight;

        GlyphArrangement g;
        g.addLineOfText (currentSection->font,
                         atom->getText(),
                         atomX, 0.0f);

        if (indexToFind - indexInText >= g.getNumGlyphs())
            return atomRight;

        return jmin (atomRight, g.getGlyph (indexToFind - indexInText).getLeft());
    }

    int xToIndex (const float xToFind) const
    {
        if (xToFind <= atomX || atom->isNewLine())
            return indexInText;

        if (xToFind >= atomRight)
            return indexInText + atom->numChars;

        GlyphArrangement g;
        g.addLineOfText (currentSection->font,
                         atom->getText(),
                         atomX, 0.0f);

        const int numGlyphs = g.getNumGlyphs();

        int j;
        for (j = 0; j < numGlyphs; ++j)
        {
            const PositionedGlyph& pg = g.getGlyph(j);
            if ((pg.getLeft() + pg.getRight()) / 2 > xToFind)
                break;
        }

        return indexInText + j;
    }

    //==============================================================================
    bool getCharPosition (const int index, float& cx, float& cy, float& lineHeightFound)
    {
        while (next())
        {
            if (indexInText + atom->numChars > index)
            {
                cx = indexToX (index);
                cy = lineY;
                lineHeightFound = lineHeight;
                return true;
            }
        }

        cx = atomX;
        cy = lineY;
        lineHeightFound = lineHeight;
        return false;
    }

    //==============================================================================
    int indexInText;
    float lineY, lineHeight, maxDescent;
    float atomX, atomRight;
    const TextAtom* atom;
    const UniformTextSection* currentSection;
    Justification justification;

private:
    const OwnedArray<UniformTextSection>& sections;
    int sectionIndex, atomIndex;
    const float wordWrapWidth;
    TextAtom tempAtom;

    Iterator& operator= (const Iterator&);

    void moveToEndOfLastAtom()
    {
        if (atom != nullptr)
        {
            atomX = atomRight;

            if (atom->isNewLine())
            {
                atomX = 0.0f;
                lineY += lineHeight;
            }
        }
    }

    bool shouldWrap (const float x) const noexcept
    {
        return (x - 0.0001f) >= wordWrapWidth;
    }

    JUCE_LEAK_DETECTOR (Iterator)
};


//==============================================================================
class SingleLineTextEditor::InsertAction  : public UndoableAction
{
public:
    InsertAction (SingleLineTextEditor& ed,
                  const String& newText,
                  const int insertPos,
                  const Font& newFont,
                  const Colour newColour,
                  const int oldCaret,
                  const int newCaret)
        : owner (ed),
          text (newText),
          insertIndex (insertPos),
          oldCaretPos (oldCaret),
          newCaretPos (newCaret),
          font (newFont),
          colour (newColour)
    {
    }

    bool perform() override
    {
        owner.insert (text, insertIndex, font, colour, nullptr, newCaretPos);
        return true;
    }

    bool undo() override
    {
        owner.remove (Range<int> (insertIndex, insertIndex + text.length()), nullptr, oldCaretPos);
        return true;
    }

    int getSizeInUnits() override
    {
        return text.length() + 16;
    }

private:
    SingleLineTextEditor& owner;
    const String text;
    const int insertIndex, oldCaretPos, newCaretPos;
    const Font font;
    const Colour colour;

    JUCE_DECLARE_NON_COPYABLE (InsertAction)
};

//==============================================================================
class SingleLineTextEditor::RemoveAction  : public UndoableAction
{
public:
    RemoveAction (SingleLineTextEditor& ed,
                  const Range<int> rangeToRemove,
                  const int oldCaret,
                  const int newCaret,
                  const Array<UniformTextSection*>& oldSections)
        : owner (ed),
          range (rangeToRemove),
          oldCaretPos (oldCaret),
          newCaretPos (newCaret)
    {
        removedSections.addArray (oldSections);
    }

    bool perform() override
    {
        owner.remove (range, nullptr, newCaretPos);
        return true;
    }

    bool undo() override
    {
        owner.reinsert (range.getStart(), removedSections);
        owner.moveCaretTo (oldCaretPos, false);
        return true;
    }

    int getSizeInUnits() override
    {
        int n = 16;
        for (int i = removedSections.size(); --i >= 0;)
            n += removedSections.getUnchecked (i)->getTotalLength();

        return n;
    }

private:
    SingleLineTextEditor& owner;
    const Range<int> range;
    const int oldCaretPos, newCaretPos;
    OwnedArray<UniformTextSection> removedSections;

    JUCE_DECLARE_NON_COPYABLE (RemoveAction)
};

//==============================================================================
class SingleLineTextEditor::TextHolderComponent  : public Component,
                                                   public Timer,
                                                   public Value::Listener
{
public:
    TextHolderComponent (SingleLineTextEditor& ed)  : owner (ed)
    {
        setWantsKeyboardFocus (false);
        setInterceptsMouseClicks (false, true);
        setMouseCursor (MouseCursor::ParentCursor);

        owner.getTextValue().addListener (this);
    }

    ~TextHolderComponent() override
    {
        owner.getTextValue().removeListener (this);
    }

    void paint (Graphics& g) override
    {
        owner.drawContent (g);
    }

    void restartTimer()
    {
        startTimer (350);
    }

    void timerCallback() override
    {
        owner.timerCallbackInt();
    }

    void valueChanged (Value&) override
    {
        owner.textWasChangedByValue();
    }

private:
    SingleLineTextEditor& owner;

    JUCE_DECLARE_NON_COPYABLE (TextHolderComponent)
};

//==============================================================================
namespace TextEditorDefs
{
    const int textChangeMessageId = 0x10003001;
    const int returnKeyMessageId  = 0x10003002;
    const int escapeKeyMessageId  = 0x10003003;
    const int focusLossMessageId  = 0x10003004;

    const int maxActionsPerTransaction = 100;

    static int getCharacterCategory (const juce_wchar character)
    {
        return CharacterFunctions::isLetterOrDigit (character)
                    ? 2 : (CharacterFunctions::isWhitespace (character) ? 0 : 1);
    }
}

//==============================================================================
SingleLineTextEditor::SingleLineTextEditor (const String& name)
    : Component (name),
      readOnly (false),
      caretVisible (true),
      popupMenuEnabled (true),
      selectAllTextWhenFocused (false),
      wasFocused (false),
      tabKeyUsed (false),
      menuActive (false),
      valueTextNeedsUpdating (false),
      consumeEscAndReturnKeys (true),
      lastTransactionTime (0),
      currentFont (14.0f),
      totalNumChars (0),
      caretPosition (0),
      keyboardType (TextInputTarget::textKeyboard),
      dragType (notDragging)
{
    setOpaque (true);
    setMouseCursor (MouseCursor::IBeamCursor);

    textHolder = std::make_unique<TextHolderComponent> (*this);
    addAndMakeVisible (*textHolder);

    setWantsKeyboardFocus (true);
    recreateCaret();
}

SingleLineTextEditor::~SingleLineTextEditor()
{
    if (wasFocused)
        if (ComponentPeer* const peer = getPeer())
            peer->dismissPendingTextInput();

    textValue.removeListener (textHolder.get());
    textValue.referTo (Value());

    textHolder = nullptr;
}

//==============================================================================
void SingleLineTextEditor::newTransaction()
{
    lastTransactionTime = Time::getApproximateMillisecondCounter();
    undoManager.beginNewTransaction();
}

bool SingleLineTextEditor::undoOrRedo (const bool shouldUndo)
{
    if (! isReadOnly())
    {
        newTransaction();

        if (shouldUndo ? undoManager.undo()
                       : undoManager.redo())
        {
            repaint();
            textChanged();
            return true;
        }
    }

    return false;
}

bool SingleLineTextEditor::undo()     { return undoOrRedo (true); }
bool SingleLineTextEditor::redo()     { return undoOrRedo (false); }

//==============================================================================
bool SingleLineTextEditor::isReadOnly() const noexcept
{
    return readOnly || ! isEnabled();
}

bool SingleLineTextEditor::isTextInputActive() const
{
    return ! isReadOnly();
}

void SingleLineTextEditor::setTabKeyUsedAsCharacter (const bool shouldTabKeyBeUsed)
{
    tabKeyUsed = shouldTabKeyBeUsed;
}

void SingleLineTextEditor::setPopupMenuEnabled (const bool b)
{
    popupMenuEnabled = b;
}

void SingleLineTextEditor::setSelectAllWhenFocused (const bool b)
{
    selectAllTextWhenFocused = b;
}

//==============================================================================
void SingleLineTextEditor::setFont (const Font& newFont)
{
    currentFont = newFont;
}

void SingleLineTextEditor::applyFontToAllText (const Font& newFont)
{
    currentFont = newFont;
    const Colour overallColour (findColour (textColourId));

    for (int i = sections.size(); --i >= 0;)
    {
        UniformTextSection* const uts = sections.getUnchecked (i);
        uts->setFont (newFont);
        uts->colour = overallColour;
    }

    coalesceSimilarSections();
    repaint();
}

void SingleLineTextEditor::colourChanged()
{
    setOpaque (findColour (backgroundColourId).isOpaque());
    repaint();
}

void SingleLineTextEditor::lookAndFeelChanged()
{
    caret = nullptr;
    recreateCaret();
    repaint();
}

void SingleLineTextEditor::enablementChanged()
{
    recreateCaret();
    repaint();
}

void SingleLineTextEditor::setCaretVisible (const bool shouldCaretBeVisible)
{
    if (caretVisible != shouldCaretBeVisible)
    {
        caretVisible = shouldCaretBeVisible;
        recreateCaret();
    }
}

void SingleLineTextEditor::recreateCaret()
{
    if (isCaretVisible())
    {
        if (caret == nullptr)
        {
            caret.reset (getLookAndFeel().createCaretComponent (this));
            textHolder->addChildComponent (*caret);
            updateCaretPosition();
        }
    }
    else
    {
        caret = nullptr;
    }
}

void SingleLineTextEditor::updateCaretPosition()
{
    if (caret != nullptr)
        caret->setCaretPosition (getCaretRectangle());
}

SingleLineTextEditor::LengthAndCharacterRestriction::LengthAndCharacterRestriction (int maxLen, const String& chars)
    : allowedCharacters (chars), maxLength (maxLen)
{}

String SingleLineTextEditor::LengthAndCharacterRestriction::filterNewText (SingleLineTextEditor& ed, const String& newInput)
{
    String t (newInput);

    if (allowedCharacters.isNotEmpty())
        t = t.retainCharacters (allowedCharacters);

    if (maxLength > 0)
        t = t.substring (0, maxLength - (ed.getTotalNumChars() - ed.getHighlightedRegion().getLength()));

    return t;
}

void SingleLineTextEditor::setInputFilter (InputFilter* newFilter, bool takeOwnership)
{
    inputFilter.set (newFilter, takeOwnership);
}

void SingleLineTextEditor::setInputRestrictions (const int maxLen, const String& chars)
{
    setInputFilter (new LengthAndCharacterRestriction (maxLen, chars), true);
}

void SingleLineTextEditor::setTextToShowWhenEmpty (const String& text, Colour colourToUse)
{
    textToShowWhenEmpty = text;
    colourForTextWhenEmpty = colourToUse;
}

//==============================================================================
void SingleLineTextEditor::clear()
{
    clearInternal (nullptr);
    undoManager.clearUndoHistory();
}

void SingleLineTextEditor::setText (const String& newText,
                          const bool sendTextChangeMessage)
{
    const int newLength = newText.length();

    if (newLength != getTotalNumChars() || getText() != newText)
    {
        textValue = newText;

        int oldCursorPos = caretPosition;
        const bool cursorWasAtEnd = oldCursorPos >= getTotalNumChars();

        clearInternal (nullptr);
        insert (newText, 0, currentFont, findColour (textColourId), nullptr, caretPosition);

        if (cursorWasAtEnd)
            oldCursorPos = getTotalNumChars();

        moveCaretTo (oldCursorPos, false);

        if (sendTextChangeMessage)
            textChanged();

        undoManager.clearUndoHistory();

        repaint();
    }
}

//==============================================================================
void SingleLineTextEditor::updateValueFromText()
{
    if (valueTextNeedsUpdating)
    {
        valueTextNeedsUpdating = false;
        textValue = getText();
    }
}

Value& SingleLineTextEditor::getTextValue()
{
    updateValueFromText();
    return textValue;
}

void SingleLineTextEditor::textWasChangedByValue()
{
    if (textValue.getValueSource().getReferenceCount() > 1)
        setText (textValue.getValue());
}

//==============================================================================
void SingleLineTextEditor::textChanged()
{
    if (listeners.size() > 0)
        postCommandMessage (TextEditorDefs::textChangeMessageId);

    if (textValue.getValueSource().getReferenceCount() > 1)
    {
        valueTextNeedsUpdating = false;
        textValue = getText();
    }
}

void SingleLineTextEditor::returnPressed()    { postCommandMessage (TextEditorDefs::returnKeyMessageId); }
void SingleLineTextEditor::escapePressed()    { postCommandMessage (TextEditorDefs::escapeKeyMessageId); }

void SingleLineTextEditor::addListener (SingleLineTextEditor::Listener* const l)      { listeners.add (l); }
void SingleLineTextEditor::removeListener (SingleLineTextEditor::Listener* const l)   { listeners.remove (l); }

//==============================================================================
void SingleLineTextEditor::timerCallbackInt()
{
    if (hasKeyboardFocus (false) && ! isCurrentlyBlockedByAnotherModalComponent())
        wasFocused = true;

    const unsigned int now = Time::getApproximateMillisecondCounter();

    if (now > lastTransactionTime + 200)
        newTransaction();
}

void SingleLineTextEditor::repaintText (const Range<int> range)
{
    if (! range.isEmpty())
    {
        float x = 0, y = 0, lh = currentFont.getHeight();

        const float wordWrapWidth = getWordWrapWidth();

        if (wordWrapWidth > 0)
        {
            Iterator i (sections, wordWrapWidth, justification);

            i.getCharPosition (range.getStart(), x, y, lh);

            const int y1 = (int) y;
            int y2;

            if (range.getEnd() >= getTotalNumChars())
            {
                y2 = textHolder->getHeight();
            }
            else
            {
                i.getCharPosition (range.getEnd(), x, y, lh);
                y2 = (int) (y + lh * 2.0f);
            }

            textHolder->repaint (0, y1, textHolder->getWidth(), y2 - y1);
        }
    }
}

//==============================================================================
void SingleLineTextEditor::moveCaret (int newCaretPos)
{
    if (newCaretPos < 0)
        newCaretPos = 0;
    else
        newCaretPos = jmin (newCaretPos, getTotalNumChars());

    if (newCaretPos != getCaretPosition())
    {
        caretPosition = newCaretPos;
        textHolder->restartTimer();
        updateCaretPosition();
    }
}

int SingleLineTextEditor::getCaretPosition() const
{
    return caretPosition;
}

void SingleLineTextEditor::setCaretPosition (const int newIndex)
{
    moveCaretTo (newIndex, false);
}

void SingleLineTextEditor::moveCaretToEnd()
{
    moveCaretTo (std::numeric_limits<int>::max(), false);
}

Rectangle<int> SingleLineTextEditor::getCaretRectangle()
{
    float cursorX, cursorY;
    float cursorHeight = currentFont.getHeight(); // (in case the text is empty and the call below doesn't set this value)
    getCharPosition (caretPosition, cursorX, cursorY, cursorHeight);

    return Rectangle<int> (roundToInt (cursorX), roundToInt (cursorY), 2, roundToInt (cursorHeight));
}

//==============================================================================
enum { rightEdgeSpace = 2 };

float SingleLineTextEditor::getWordWrapWidth() const
{
    return std::numeric_limits<float>::max();
}

int SingleLineTextEditor::getTextWidth() const    { return textHolder->getWidth(); }
int SingleLineTextEditor::getTextHeight() const   { return textHolder->getHeight(); }

void SingleLineTextEditor::moveCaretTo (const int newPosition, const bool isSelecting)
{
    if (isSelecting)
    {
        moveCaret (newPosition);

        const Range<int> oldSelection (selection);

        if (dragType == notDragging)
        {
            if (abs (getCaretPosition() - selection.getStart()) < abs (getCaretPosition() - selection.getEnd()))
                dragType = draggingSelectionStart;
            else
                dragType = draggingSelectionEnd;
        }

        if (dragType == draggingSelectionStart)
        {
            if (getCaretPosition() >= selection.getEnd())
                dragType = draggingSelectionEnd;

            selection = Range<int>::between (getCaretPosition(), selection.getEnd());
        }
        else
        {
            if (getCaretPosition() < selection.getStart())
                dragType = draggingSelectionStart;

            selection = Range<int>::between (getCaretPosition(), selection.getStart());
        }

        repaintText (selection.getUnionWith (oldSelection));
    }
    else
    {
        dragType = notDragging;

        repaintText (selection);

        moveCaret (newPosition);
        selection = Range<int>::emptyRange (getCaretPosition());
    }
}

int SingleLineTextEditor::getTextIndexAt (const int x, const int y)
{
    return indexAtPosition ((float) x, (float) y);
}

void SingleLineTextEditor::insertTextAtCaret (const String& t)
{
    String newText (inputFilter != nullptr ? inputFilter->filterNewText (*this, t) : t);

    newText = newText.replaceCharacters ("\r\n", "  ");

    const int insertIndex = selection.getStart();
    const int newCaretPos = insertIndex + newText.length();

    remove (selection, getUndoManager(),
            newText.isNotEmpty() ? newCaretPos - 1 : newCaretPos);

    insert (newText, insertIndex, currentFont, findColour (textColourId),
            getUndoManager(), newCaretPos);

    textChanged();
}

void SingleLineTextEditor::setHighlightedRegion (const Range<int>& newSelection)
{
    moveCaretTo (newSelection.getStart(), false);
    moveCaretTo (newSelection.getEnd(), true);
}

//==============================================================================
void SingleLineTextEditor::copy()
{
    const String selectedText (getHighlightedText());

    if (selectedText.isNotEmpty())
        SystemClipboard::copyTextToClipboard (selectedText);
}

void SingleLineTextEditor::paste()
{
    if (! isReadOnly())
    {
        const String clip (SystemClipboard::getTextFromClipboard());

        if (clip.isNotEmpty())
            insertTextAtCaret (clip);
    }
}

void SingleLineTextEditor::cut()
{
    if (! isReadOnly())
    {
        moveCaret (selection.getEnd());
        insertTextAtCaret (String());
    }
}

//==============================================================================
void SingleLineTextEditor::drawContent (Graphics& g)
{
    Rectangle<int> r = getLocalBounds();

    GlyphArrangement ga;
    ga.addFittedText (getFont(), getText(),
                      float (r.getX()), float (r.getY()),
                      float (r.getWidth()), float (r.getHeight()), justification, 1);

    Colour selectedTextColour = findColour (highlightedTextColourId);
    Colour normalTextColour   = findColour (textColourId);

    if (! selection.isEmpty())
    {
        g.setColour (findColour (highlightColourId).withMultipliedAlpha (hasKeyboardFocus (true) ? 1.0f : 0.5f));

        auto s = selection.getStart();
        auto e = selection.getEnd();
        
        float x1, y1, x2, y2, h1, h2;
        getCharPosition (s, x1, y1, h1);
        getCharPosition (e, x2, y2, h2);

        g.fillRect (x1, y1, x2 - x1, h1);
    }

    for (int i = 0; i < ga.getNumGlyphs(); i++)
    {
        if (selection.contains (i))
            g.setColour (selectedTextColour);
        else
            g.setColour (normalTextColour);

        ga.getGlyph (i).draw (g);
    }
}

void SingleLineTextEditor::paint (Graphics& g)
{
    if (LookAndFeelMethods* lfm = dynamic_cast<LookAndFeelMethods*> (&getLookAndFeel()))
        lfm->fillSingleLineTextEditorBackground (g, getWidth(), getHeight(), *this);
}

void SingleLineTextEditor::paintOverChildren (Graphics& g)
{
    if (textToShowWhenEmpty.isNotEmpty()
         && (! hasKeyboardFocus (false))
         && getTotalNumChars() == 0)
    {
        g.setColour (colourForTextWhenEmpty);
        g.setFont (getFont());

        g.drawText (textToShowWhenEmpty,
                    0, 0, getWidth(), getHeight(),
                    justification, true);
    }

    if (LookAndFeelMethods* lfm = dynamic_cast<LookAndFeelMethods*> (&getLookAndFeel()))
        lfm->drawSingleLineTextEditorOutline (g, getWidth(), getHeight(), *this);
}

//==============================================================================
void SingleLineTextEditor::addPopupMenuItems (PopupMenu& m, const MouseEvent*)
{
    const bool writable = ! isReadOnly();

    m.addItem (StandardApplicationCommandIDs::cut,       TRANS("Cut"), writable);
    m.addItem (StandardApplicationCommandIDs::copy,      TRANS("Copy"), ! selection.isEmpty());
    m.addItem (StandardApplicationCommandIDs::paste,     TRANS("Paste"), writable);
    m.addItem (StandardApplicationCommandIDs::del,       TRANS("Delete"), writable);
    m.addSeparator();
    m.addItem (StandardApplicationCommandIDs::selectAll, TRANS("Select All"));
    m.addSeparator();

    if (getUndoManager() != nullptr)
    {
        m.addItem (StandardApplicationCommandIDs::undo, TRANS("Undo"), undoManager.canUndo());
        m.addItem (StandardApplicationCommandIDs::redo, TRANS("Redo"), undoManager.canRedo());
    }
}

void SingleLineTextEditor::performPopupMenuAction (const int menuItemID)
{
    switch (menuItemID)
    {
        case StandardApplicationCommandIDs::cut:        cutToClipboard(); break;
        case StandardApplicationCommandIDs::copy:       copyToClipboard(); break;
        case StandardApplicationCommandIDs::paste:      pasteFromClipboard(); break;
        case StandardApplicationCommandIDs::del:        cut(); break;
        case StandardApplicationCommandIDs::selectAll:  selectAll(); break;
        case StandardApplicationCommandIDs::undo:       undo(); break;
        case StandardApplicationCommandIDs::redo:       redo(); break;
        default: break;
    }
}

static void textEditorMenuCallback (int menuResult, SingleLineTextEditor* editor)
{
    if (editor != nullptr && menuResult != 0)
        editor->performPopupMenuAction (menuResult);
}

//==============================================================================
void SingleLineTextEditor::mouseDown (const MouseEvent& e)
{
    beginDragAutoRepeat (100);
    newTransaction();

    if (wasFocused || ! selectAllTextWhenFocused)
    {
        if (! (popupMenuEnabled && e.mods.isPopupMenu()))
        {
            moveCaretTo (getTextIndexAt (e.x, e.y),
                         e.mods.isShiftDown());
        }
        else
        {
            PopupMenu m;
            m.setLookAndFeel (&getLookAndFeel());
            addPopupMenuItems (m, &e);

            m.showMenuAsync (PopupMenu::Options(),
                             ModalCallbackFunction::forComponent (textEditorMenuCallback, this));
        }
    }
}

void SingleLineTextEditor::mouseDrag (const MouseEvent& e)
{
    if (wasFocused || ! selectAllTextWhenFocused)
        if (! (popupMenuEnabled && e.mods.isPopupMenu()))
            moveCaretTo (getTextIndexAt (e.x, e.y), true);
}

void SingleLineTextEditor::mouseUp (const MouseEvent& e)
{
    newTransaction();
    textHolder->restartTimer();

    if (wasFocused || ! selectAllTextWhenFocused)
        if (e.mouseWasClicked() && ! (popupMenuEnabled && e.mods.isPopupMenu()))
            moveCaret (getTextIndexAt (e.x, e.y));

    wasFocused = true;
}

void SingleLineTextEditor::mouseDoubleClick (const MouseEvent& e)
{
    int tokenEnd = getTextIndexAt (e.x, e.y);
    int tokenStart = 0;

    if (e.getNumberOfClicks() > 3)
    {
        tokenEnd = getTotalNumChars();
    }
    else
    {
        const String t (getText());
        const int totalLength = getTotalNumChars();

        while (tokenEnd < totalLength)
        {
            // (note the slight bodge here - it's because iswalnum only checks for alphabetic chars in the current locale)
            const juce_wchar c = t [tokenEnd];
            if (CharacterFunctions::isLetterOrDigit (c) || c > 128)
                ++tokenEnd;
            else
                break;
        }

        tokenStart = tokenEnd;

        while (tokenStart > 0)
        {
            // (note the slight bodge here - it's because iswalnum only checks for alphabetic chars in the current locale)
            const juce_wchar c = t [tokenStart - 1];
            if (CharacterFunctions::isLetterOrDigit (c) || c > 128)
                --tokenStart;
            else
                break;
        }

        if (e.getNumberOfClicks() > 2)
        {
            while (tokenEnd < totalLength)
            {
                const juce_wchar c = t [tokenEnd];
                if (c != '\r' && c != '\n')
                    ++tokenEnd;
                else
                    break;
            }

            while (tokenStart > 0)
            {
                const juce_wchar c = t [tokenStart - 1];
                if (c != '\r' && c != '\n')
                    --tokenStart;
                else
                    break;
            }
        }
    }

    moveCaretTo (tokenEnd, false);
    moveCaretTo (tokenStart, true);
}

void SingleLineTextEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    Component::mouseWheelMove (e, wheel);
}

//==============================================================================
bool SingleLineTextEditor::moveCaretWithTransaction (const int newPos, const bool selecting)
{
    newTransaction();
    moveCaretTo (newPos, selecting);
    return true;
}

bool SingleLineTextEditor::moveCaretLeft (bool moveInWholeWordSteps, bool selecting)
{
    int pos = getCaretPosition();

    if (moveInWholeWordSteps)
        pos = findWordBreakBefore (pos);
    else
        --pos;

    return moveCaretWithTransaction (pos, selecting);
}

bool SingleLineTextEditor::moveCaretRight (bool moveInWholeWordSteps, bool selecting)
{
    int pos = getCaretPosition();

    if (moveInWholeWordSteps)
        pos = findWordBreakAfter (pos);
    else
        ++pos;

    return moveCaretWithTransaction (pos, selecting);
}

bool SingleLineTextEditor::moveCaretUp (bool selecting)
{
    return moveCaretToStartOfLine (selecting);
}

bool SingleLineTextEditor::moveCaretDown (bool selecting)
{
    return moveCaretToEndOfLine (selecting);
}

bool SingleLineTextEditor::pageUp (bool selecting)
{
    return moveCaretToStartOfLine (selecting);
}

bool SingleLineTextEditor::pageDown (bool selecting)
{
    return moveCaretToEndOfLine (selecting);
}

bool SingleLineTextEditor::moveCaretToTop (bool selecting)
{
    return moveCaretWithTransaction (0, selecting);
}

bool SingleLineTextEditor::moveCaretToStartOfLine (bool selecting)
{
    const Rectangle<float> caretPos (getCaretRectangle().toFloat());
    return moveCaretWithTransaction (indexAtPosition (0.0f, caretPos.getY()), selecting);
}

bool SingleLineTextEditor::moveCaretToEnd (bool selecting)
{
    return moveCaretWithTransaction (getTotalNumChars(), selecting);
}

bool SingleLineTextEditor::moveCaretToEndOfLine (bool selecting)
{
    const Rectangle<float> caretPos (getCaretRectangle().toFloat());
    return moveCaretWithTransaction (indexAtPosition ((float) textHolder->getWidth(), caretPos.getY()), selecting);
}

bool SingleLineTextEditor::deleteBackwards (bool moveInWholeWordSteps)
{
    if (moveInWholeWordSteps)
        moveCaretTo (findWordBreakBefore (getCaretPosition()), true);
    else if (selection.isEmpty() && selection.getStart() > 0)
        selection = Range<int> (selection.getEnd() - 1, selection.getEnd());

    cut();
    return true;
}

bool SingleLineTextEditor::deleteForwards (bool /*moveInWholeWordSteps*/)
{
    if (selection.isEmpty() && selection.getStart() < getTotalNumChars())
        selection = Range<int> (selection.getStart(), selection.getStart() + 1);

    cut();
    return true;
}

bool SingleLineTextEditor::copyToClipboard()
{
    newTransaction();
    copy();
    return true;
}

bool SingleLineTextEditor::cutToClipboard()
{
    newTransaction();
    copy();
    cut();
    return true;
}

bool SingleLineTextEditor::pasteFromClipboard()
{
    newTransaction();
    paste();
    return true;
}

bool SingleLineTextEditor::selectAll()
{
    newTransaction();
    moveCaretTo (0, false);
    moveCaretTo (getTotalNumChars(), true);
    return true;
}

//==============================================================================
void SingleLineTextEditor::setEscapeAndReturnKeysConsumed (bool shouldBeConsumed) noexcept
{
    consumeEscAndReturnKeys = shouldBeConsumed;
}

bool SingleLineTextEditor::keyPressed (const KeyPress& key)
{
    if (isReadOnly() && key != KeyPress ('c', ModifierKeys::commandModifier, 0))
        return false;

    if (! TextEditorKeyMapper<SingleLineTextEditor>::invokeKeyFunction (*this, key))
    {
        if (key == KeyPress::returnKey)
        {
            newTransaction();

            returnPressed();
            return consumeEscAndReturnKeys;
        }
        else if (key.isKeyCode (KeyPress::escapeKey))
        {
            newTransaction();
            moveCaretTo (getCaretPosition(), false);
            escapePressed();
            return consumeEscAndReturnKeys;
        }
        else if (key.getTextCharacter() >= ' '
                  || (tabKeyUsed && (key.getTextCharacter() == '\t')))
        {
            insertTextAtCaret (String::charToString (key.getTextCharacter()));

            lastTransactionTime = Time::getApproximateMillisecondCounter();
        }
        else
        {
            return false;
        }
    }

    return true;
}

bool SingleLineTextEditor::keyStateChanged (const bool isKeyDown)
{
    if (! isKeyDown)
        return false;

   #if JUCE_WINDOWS
    if (KeyPress (KeyPress::F4Key, ModifierKeys::altModifier, 0).isCurrentlyDown())
        return false;  // We need to explicitly allow alt-F4 to pass through on Windows
   #endif

    if ((! consumeEscAndReturnKeys)
         && (KeyPress (KeyPress::escapeKey).isCurrentlyDown()
          || KeyPress (KeyPress::returnKey).isCurrentlyDown()))
        return false;

    // (overridden to avoid forwarding key events to the parent)
    return ! ModifierKeys::getCurrentModifiers().isCommandDown();
}

//==============================================================================
void SingleLineTextEditor::focusGained (FocusChangeType)
{
    newTransaction();

    if (selectAllTextWhenFocused)
    {
        moveCaretTo (0, false);
        moveCaretTo (getTotalNumChars(), true);
    }

    repaint();
    updateCaretPosition();

    if (ComponentPeer* const peer = getPeer())
        if (! isReadOnly())
            peer->textInputRequired (peer->globalToLocal (getScreenPosition()), *this);
}

void SingleLineTextEditor::focusLost (FocusChangeType)
{
    newTransaction();

    wasFocused = false;
    textHolder->stopTimer();

    underlinedSections.clear();

    if (ComponentPeer* const peer = getPeer())
        peer->dismissPendingTextInput();

    updateCaretPosition();

    postCommandMessage (TextEditorDefs::focusLossMessageId);
    repaint();
}

//==============================================================================
void SingleLineTextEditor::resized()
{
    textHolder->setBounds (getLocalBounds());
    updateCaretPosition();
}

void SingleLineTextEditor::handleCommandMessage (const int commandId)
{
    Component::BailOutChecker checker (this);

    switch (commandId)
    {
    case TextEditorDefs::textChangeMessageId:
        listeners.callChecked (checker, &SingleLineTextEditor::Listener::sltextEditorTextChanged, (SingleLineTextEditor&) *this);
        break;

    case TextEditorDefs::returnKeyMessageId:
        listeners.callChecked (checker, &SingleLineTextEditor::Listener::sltextEditorReturnKeyPressed, (SingleLineTextEditor&) *this);
        break;

    case TextEditorDefs::escapeKeyMessageId:
        listeners.callChecked (checker, &SingleLineTextEditor::Listener::sltextEditorEscapeKeyPressed, (SingleLineTextEditor&) *this);
        break;

    case TextEditorDefs::focusLossMessageId:
        updateValueFromText();
        listeners.callChecked (checker, &SingleLineTextEditor::Listener::sltextEditorFocusLost, (SingleLineTextEditor&) *this);
        break;

    default:
        jassertfalse;
        break;
    }
}

void SingleLineTextEditor::setTemporaryUnderlining (const Array<Range<int> >& newUnderlinedSections)
{
    underlinedSections = newUnderlinedSections;
    repaint();
}

//==============================================================================
UndoManager* SingleLineTextEditor::getUndoManager() noexcept
{
    return readOnly ? nullptr : &undoManager;
}

void SingleLineTextEditor::clearInternal (UndoManager* const um)
{
    remove (Range<int> (0, getTotalNumChars()), um, caretPosition);
}

void SingleLineTextEditor::insert (const String& text,
                         const int insertIndex,
                         const Font& font,
                         const Colour colour,
                         UndoManager* const um,
                         const int caretPositionToMoveTo)
{
    if (text.isNotEmpty())
    {
        if (um != nullptr)
        {
            if (um->getNumActionsInCurrentTransaction() > TextEditorDefs::maxActionsPerTransaction)
                newTransaction();

            um->perform (new InsertAction (*this, text, insertIndex, font, colour,
                                           caretPosition, caretPositionToMoveTo));
        }
        else
        {
            repaintText (Range<int> (insertIndex, getTotalNumChars())); // must do this before and after changing the data, in case
                                                                        // a line gets moved due to word wrap

            int index = 0;
            int nextIndex = 0;

            for (int i = 0; i < sections.size(); ++i)
            {
                nextIndex = index + sections.getUnchecked (i)->getTotalLength();

                if (insertIndex == index)
                {
                    sections.insert (i, new UniformTextSection (text, font, colour));
                    break;
                }
                else if (insertIndex > index && insertIndex < nextIndex)
                {
                    splitSection (i, insertIndex - index);
                    sections.insert (i + 1, new UniformTextSection (text, font, colour));
                    break;
                }

                index = nextIndex;
            }

            if (nextIndex == insertIndex)
                sections.add (new UniformTextSection (text, font, colour));

            coalesceSimilarSections();
            totalNumChars = -1;
            valueTextNeedsUpdating = true;

            moveCaretTo (caretPositionToMoveTo, false);

            repaintText (Range<int> (insertIndex, getTotalNumChars()));
        }
    }
}

void SingleLineTextEditor::reinsert (const int insertIndex, const OwnedArray<UniformTextSection>& sectionsToInsert)
{
    int index = 0;
    int nextIndex = 0;

    for (int i = 0; i < sections.size(); ++i)
    {
        nextIndex = index + sections.getUnchecked (i)->getTotalLength();

        if (insertIndex == index)
        {
            for (int j = sectionsToInsert.size(); --j >= 0;)
                sections.insert (i, new UniformTextSection (*sectionsToInsert.getUnchecked(j)));

            break;
        }
        else if (insertIndex > index && insertIndex < nextIndex)
        {
            splitSection (i, insertIndex - index);

            for (int j = sectionsToInsert.size(); --j >= 0;)
                sections.insert (i + 1, new UniformTextSection (*sectionsToInsert.getUnchecked(j)));

            break;
        }

        index = nextIndex;
    }

    if (nextIndex == insertIndex)
    {
        for (int j = 0; j < sectionsToInsert.size(); ++j)
            sections.add (new UniformTextSection (*sectionsToInsert.getUnchecked(j)));
    }

    coalesceSimilarSections();
    totalNumChars = -1;
    valueTextNeedsUpdating = true;
}

void SingleLineTextEditor::remove (Range<int> range, UndoManager* const um, const int caretPositionToMoveTo)
{
    if (! range.isEmpty())
    {
        int index = 0;

        for (int i = 0; i < sections.size(); ++i)
        {
            const int nextIndex = index + sections.getUnchecked(i)->getTotalLength();

            if (range.getStart() > index && range.getStart() < nextIndex)
            {
                splitSection (i, range.getStart() - index);
                --i;
            }
            else if (range.getEnd() > index && range.getEnd() < nextIndex)
            {
                splitSection (i, range.getEnd() - index);
                --i;
            }
            else
            {
                index = nextIndex;

                if (index > range.getEnd())
                    break;
            }
        }

        index = 0;

        if (um != nullptr)
        {
            Array<UniformTextSection*> removedSections;

            for (int i = 0; i < sections.size(); ++i)
            {
                if (range.getEnd() <= range.getStart())
                    break;

                UniformTextSection* const section = sections.getUnchecked (i);

                const int nextIndex = index + section->getTotalLength();

                if (range.getStart() <= index && range.getEnd() >= nextIndex)
                    removedSections.add (new UniformTextSection (*section));

                index = nextIndex;
            }

            if (um->getNumActionsInCurrentTransaction() > TextEditorDefs::maxActionsPerTransaction)
                newTransaction();

            um->perform (new RemoveAction (*this, range, caretPosition,
                                           caretPositionToMoveTo, removedSections));
        }
        else
        {
            Range<int> remainingRange (range);

            for (int i = 0; i < sections.size(); ++i)
            {
                UniformTextSection* const section = sections.getUnchecked (i);

                const int nextIndex = index + section->getTotalLength();

                if (remainingRange.getStart() <= index && remainingRange.getEnd() >= nextIndex)
                {
                    sections.remove (i);

                    remainingRange.setEnd (remainingRange.getEnd() - (nextIndex - index));
                    if (remainingRange.isEmpty())
                        break;

                    --i;
                }
                else
                {
                    index = nextIndex;
                }
            }

            coalesceSimilarSections();
            totalNumChars = -1;
            valueTextNeedsUpdating = true;

            moveCaretTo (caretPositionToMoveTo, false);

            repaintText (Range<int> (range.getStart(), getTotalNumChars()));
        }
    }
}

//==============================================================================
String SingleLineTextEditor::getText() const
{
    MemoryOutputStream mo;
    mo.preallocate ((size_t) getTotalNumChars());

    for (int i = 0; i < sections.size(); ++i)
        sections.getUnchecked (i)->appendAllText (mo);

    return mo.toUTF8();
}

String SingleLineTextEditor::getTextInRange (const Range<int>& range) const
{
    if (range.isEmpty())
        return String();

    MemoryOutputStream mo;
    mo.preallocate ((size_t) jmin (getTotalNumChars(), range.getLength()));

    int index = 0;

    for (int i = 0; i < sections.size(); ++i)
    {
        const UniformTextSection* const s = sections.getUnchecked (i);
        const int nextIndex = index + s->getTotalLength();

        if (range.getStart() < nextIndex)
        {
            if (range.getEnd() <= index)
                break;

            s->appendSubstring (mo, range - index);
        }

        index = nextIndex;
    }

    return mo.toUTF8();
}

String SingleLineTextEditor::getHighlightedText() const
{
    return getTextInRange (selection);
}

int SingleLineTextEditor::getTotalNumChars() const
{
    if (totalNumChars < 0)
    {
        totalNumChars = 0;

        for (int i = sections.size(); --i >= 0;)
            totalNumChars += sections.getUnchecked (i)->getTotalLength();
    }

    return totalNumChars;
}

bool SingleLineTextEditor::isEmpty() const
{
    return getTotalNumChars() == 0;
}

void SingleLineTextEditor::getCharPosition (const int index, float& cx, float& cy, float& lineHeight) const
{
    Rectangle<int> r = getLocalBounds();

    GlyphArrangement ga;
    ga.addFittedText (getFont(), getText(),
                      float (r.getX()), float (r.getY()),
                      float (r.getWidth()), float (r.getHeight()), justification, 1);

    const int numGlyphs = ga.getNumGlyphs();
    if (numGlyphs == 0)
    {
        cx = float (r.getCentreX());
        cy = 1;
    }
    else if (index >= 0 && index < numGlyphs)
    {
        PositionedGlyph& pg = ga.getGlyph (index);

        cx = pg.getLeft();
        cy = pg.getTop();
        lineHeight = pg.getBottom() - pg.getTop();
    }
    else if (numGlyphs > 0 && numGlyphs == index)
    {
        PositionedGlyph& pg = ga.getGlyph (index - 1);

        cx = pg.getRight();
        cy = pg.getTop();
        lineHeight = pg.getBottom() - pg.getTop();
    }
    else
    {
        cx = 0;
        cy = 0;
        lineHeight = 0;
    }
}

int SingleLineTextEditor::indexAtPosition (const float x, const float)
{
    Rectangle<int> r = getLocalBounds();

    GlyphArrangement ga;
    ga.addFittedText (getFont(), getText(),
                      float (r.getX()), float (r.getY()),
                      float (r.getWidth()), float (r.getHeight()), justification, 1);

    const int numGlyphs = ga.getNumGlyphs();

    if (numGlyphs > 0)
    {
        PositionedGlyph& pg = ga.getGlyph (numGlyphs - 1);
        if (x >= pg.getRight())
            return numGlyphs;
    }

    for (int i = numGlyphs; --i >= 0;)
    {
        PositionedGlyph& pg = ga.getGlyph (i);

        if (x >= pg.getLeft())
            return i;
    }
    return getTotalNumChars();
}

//==============================================================================
int SingleLineTextEditor::findWordBreakAfter (const int position) const
{
    const String t (getTextInRange (Range<int> (position, position + 512)));
    const int totalLength = t.length();
    int i = 0;

    while (i < totalLength && CharacterFunctions::isWhitespace (t[i]))
        ++i;

    const int type = TextEditorDefs::getCharacterCategory (t[i]);

    while (i < totalLength && type == TextEditorDefs::getCharacterCategory (t[i]))
        ++i;

    while (i < totalLength && CharacterFunctions::isWhitespace (t[i]))
        ++i;

    return position + i;
}

int SingleLineTextEditor::findWordBreakBefore (const int position) const
{
    if (position <= 0)
        return 0;

    const int startOfBuffer = jmax (0, position - 512);
    const String t (getTextInRange (Range<int> (startOfBuffer, position)));

    int i = position - startOfBuffer;

    while (i > 0 && CharacterFunctions::isWhitespace (t [i - 1]))
        --i;

    if (i > 0)
    {
        const int type = TextEditorDefs::getCharacterCategory (t [i - 1]);

        while (i > 0 && type == TextEditorDefs::getCharacterCategory (t [i - 1]))
            --i;
    }

    jassert (startOfBuffer + i >= 0);
    return startOfBuffer + i;
}


//==============================================================================
void SingleLineTextEditor::splitSection (const int sectionIndex, const int charToSplitAt)
{
    jassert (sections[sectionIndex] != nullptr);

    sections.insert (sectionIndex + 1,
                     sections.getUnchecked (sectionIndex)->split (charToSplitAt));
}

void SingleLineTextEditor::coalesceSimilarSections()
{
    for (int i = 0; i < sections.size() - 1; ++i)
    {
        UniformTextSection* const s1 = sections.getUnchecked (i);
        UniformTextSection* const s2 = sections.getUnchecked (i + 1);

        if (s1->font == s2->font
             && s1->colour == s2->colour)
        {
            s1->append (*s2);
            sections.remove (i + 1);
            --i;
        }
    }
}
