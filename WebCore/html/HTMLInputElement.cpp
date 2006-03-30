/*
 * This file is part of the DOM implementation for KDE.
 *
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004, 2005, 2006 Apple Computer, Inc.
 *           (C) 2006 Alexey Proskuryakov (ap@nypop.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"
#include "HTMLInputElement.h"

#include "BeforeTextInsertedEvent.h"
#include "Document.h"
#include "EventNames.h"
#include "FormDataList.h"
#include "Frame.h"
#include "HTMLFormElement.h"
#include "LocalizedStrings.h"
#include "RenderTextField.h"
#include "SelectionController.h"
#include "CSSPropertyNames.h"
#include "dom2_eventsimpl.h"
#include "html_imageimpl.h"
#include "HTMLNames.h"
#include "render_button.h"
#include "render_form.h"
#include "RenderTheme.h"

using WebCore::ControlState;

namespace WebCore {

using namespace EventNames;
using namespace HTMLNames;

HTMLInputElement::HTMLInputElement(Document *doc, HTMLFormElement *f)
    : HTMLGenericFormElement(inputTag, doc, f)
{
    init();
}

HTMLInputElement::HTMLInputElement(const QualifiedName& tagName, Document *doc, HTMLFormElement *f)
    : HTMLGenericFormElement(tagName, doc, f)
{
    init();
}

void HTMLInputElement::init()
{
    m_imageLoader = 0;
    m_valueMatchesRenderer = false;
    m_type = TEXT;
    m_maxLen = 1024;
    m_size = 20;
    m_checked = false;
    m_defaultChecked = false;
    m_useDefaultChecked = true;
    m_indeterminate = false;

    m_haveType = false;
    m_activeSubmit = false;
    m_autocomplete = true;
    m_inited = false;

    xPos = 0;
    yPos = 0;

    m_maxResults = -1;

    if (m_form)
        m_autocomplete = m_form->autoComplete();
}

HTMLInputElement::~HTMLInputElement()
{
    document()->deregisterMaintainsState(this);
    delete m_imageLoader;
}

const AtomicString& HTMLInputElement::name() const
{
    return m_name.isNull() ? emptyAtom : m_name;
}

bool HTMLInputElement::isKeyboardFocusable() const
{
    // If text fields can be focused, then they should always be keyboardFocusable
    // FIXME:  When other text fields switch to the Non-NSView implementation, we should add them here.
    if (m_type == TEXT)
        return HTMLGenericFormElement::isFocusable();
        
    // If the base class says we can't be focused, then we can stop now.
    if (!HTMLGenericFormElement::isKeyboardFocusable())
        return false;

    if (m_type == RADIO) {
        // Unnamed radio buttons are never focusable (matches WinIE).
        if (name().isEmpty())
            return false;

        // Never allow keyboard tabbing to leave you in the same radio group.  Always
        // skip any other elements in the group.
        Node* currentFocusNode = document()->focusNode();
        if (currentFocusNode && currentFocusNode->hasTagName(inputTag)) {
            HTMLInputElement* focusedInput = static_cast<HTMLInputElement*>(currentFocusNode);
            if (focusedInput->inputType() == RADIO && focusedInput->form() == m_form &&
                focusedInput->name() == name())
                return false;
        }
        
        // Allow keyboard focus if we're checked or if nothing in the group is checked.
        return checked() || !document()->checkedRadioButtonForGroup(name().impl(), m_form);
    }
    
    return true;
}

bool HTMLInputElement::isMouseFocusable() const
{
    // FIXME:  When other text fields switch to the Non-NSView implementation, we should add them here.
    if (m_type == TEXT)
        return HTMLGenericFormElement::isFocusable();
    return HTMLGenericFormElement::isMouseFocusable();
}

void HTMLInputElement::focus()
{
    // FIXME:  When other text fields switch to the Non-NSView implementation, we should add them here.
    if (m_type == TEXT) {
        Document* doc = document();
        doc->updateLayout();
        if (isFocusable()) {
            doc->setFocusNode(this);
            select();
            doc->frame()->revealSelection();
        }
    } else
        HTMLGenericFormElement::focus();
}

void HTMLInputElement::setType(const String& t)
{
    if (t.isEmpty()) {
        int exccode;
        removeAttribute(typeAttr, exccode);
    }
    else
        setAttribute(typeAttr, t);
}

void HTMLInputElement::setInputType(const String& t)
{
    typeEnum newType;
    
    if (equalIgnoringCase(t, "password"))
        newType = PASSWORD;
    else if (equalIgnoringCase(t, "checkbox"))
        newType = CHECKBOX;
    else if (equalIgnoringCase(t, "radio"))
        newType = RADIO;
    else if (equalIgnoringCase(t, "submit"))
        newType = SUBMIT;
    else if (equalIgnoringCase(t, "reset"))
        newType = RESET;
    else if (equalIgnoringCase(t, "file"))
        newType = FILE;
    else if (equalIgnoringCase(t, "hidden"))
        newType = HIDDEN;
    else if (equalIgnoringCase(t, "image"))
        newType = IMAGE;
    else if (equalIgnoringCase(t, "button"))
        newType = BUTTON;
    else if (equalIgnoringCase(t, "khtml_isindex"))
        newType = ISINDEX;
    else if (equalIgnoringCase(t, "search"))
        newType = SEARCH;
    else if (equalIgnoringCase(t, "range"))
        newType = RANGE;
    else
        newType = TEXT;

    // ### IMPORTANT: Don't allow the type to be changed to FILE after the first
    // type change, otherwise a JavaScript programmer would be able to set a text
    // field's value to something like /etc/passwd and then change it to a file field.
    if (inputType() != newType) {
        if (newType == FILE && m_haveType) {
            // Set the attribute back to the old value.
            // Useful in case we were called from inside parseMappedAttribute.
            setAttribute(typeAttr, type());
        } else {
            if (inputType() == RADIO && !name().isEmpty()) {
                if (document()->checkedRadioButtonForGroup(name().impl(), m_form) == this)
                    document()->removeRadioButtonGroup(name().impl(), m_form);
            }
            bool wasAttached = m_attached;
            if (wasAttached)
                detach();
            bool didStoreValue = storesValueSeparateFromAttribute();
            bool didMaintainState = maintainsState();
            m_type = newType;
            bool willStoreValue = storesValueSeparateFromAttribute();
            bool willMaintainState = maintainsState();
            if (didStoreValue && !willStoreValue && !m_value.isNull()) {
                setAttribute(valueAttr, m_value);
                m_value = String();
            }
            if (!didStoreValue && willStoreValue) {
                m_value = getAttribute(valueAttr);
            }
            if (willMaintainState && !didMaintainState)
                document()->registerMaintainsState(this);
            else if (!willMaintainState && didMaintainState)
                document()->deregisterMaintainsState(this);
            if (wasAttached)
                attach();
                
            // If our type morphs into a radio button and we are checked, then go ahead
            // and signal this to the form.
            if (m_type == RADIO && checked())
                document()->radioButtonChecked(this, m_form);
        }
    }
    m_haveType = true;
    
    if (m_type != IMAGE && m_imageLoader) {
        delete m_imageLoader;
        m_imageLoader = 0;
    }
}

String HTMLInputElement::type() const
{
    // needs to be lowercase according to DOM spec
    switch (m_type) {
    case TEXT: return "text";
    case PASSWORD: return "password";
    case CHECKBOX: return "checkbox";
    case RADIO: return "radio";
    case SUBMIT: return "submit";
    case RESET: return "reset";
    case FILE: return "file";
    case HIDDEN: return "hidden";
    case IMAGE: return "image";
    case BUTTON: return "button";
    case SEARCH: return "search";
    case RANGE: return "range";
    case ISINDEX: return "";
    }
    return "";
}

DeprecatedString HTMLInputElement::state( )
{
    assert(m_type != PASSWORD); // should never save/restore password fields

    DeprecatedString state = HTMLGenericFormElement::state();
    switch (m_type) {
    case CHECKBOX:
    case RADIO:
        return state + (checked() ? "on" : "off");
    default:
        return state + value().deprecatedString()+'.'; // Make sure the string is not empty!
    }
}

void HTMLInputElement::restoreState(DeprecatedStringList &states)
{
    assert(m_type != PASSWORD); // should never save/restore password fields
    
    DeprecatedString state = HTMLGenericFormElement::findMatchingState(states);
    if (state.isNull()) return;

    switch (m_type) {
    case CHECKBOX:
    case RADIO:
        setChecked((state == "on"));
        break;
    default:
        setValue(String(state.left(state.length()-1)));
        break;
    }
}

bool HTMLInputElement::canHaveSelection()
{
    switch (m_type) {
        case TEXT:
        case PASSWORD:
        case SEARCH:
            return true;
        default:
            break;
    }
    return false;
}

int HTMLInputElement::selectionStart()
{
    if (!renderer())
        return 0;
    
    switch (m_type) {
        case TEXT:
             return static_cast<RenderTextField *>(renderer())->selectionStart();
        case PASSWORD:
        case SEARCH:
            return static_cast<RenderLineEdit *>(renderer())->selectionStart();
        default:
            break;
    }
    return 0;
}

int HTMLInputElement::selectionEnd()
{
    if (!renderer())
        return 0;
    
    switch (m_type) {
        case TEXT:
            return static_cast<RenderTextField *>(renderer())->selectionEnd();
        case PASSWORD:
        case SEARCH:
            return static_cast<RenderLineEdit *>(renderer())->selectionEnd();
        default:
            break;
    }
    return 0;
}

void HTMLInputElement::setSelectionStart(int start)
{
    if (!renderer())
        return;
    
    switch (m_type) {
        case TEXT:
            static_cast<RenderTextField *>(renderer())->setSelectionStart(start);
            break;
        case PASSWORD:
        case SEARCH:
            static_cast<RenderLineEdit *>(renderer())->setSelectionStart(start);
            break;
        default:
            break;
    }
}

void HTMLInputElement::setSelectionEnd(int end)
{
    if (!renderer())
        return;
    
    switch (m_type) {
        case TEXT:
            static_cast<RenderTextField *>(renderer())->setSelectionEnd(end);
            break;
        case PASSWORD:
        case SEARCH:
            static_cast<RenderLineEdit *>(renderer())->setSelectionEnd(end);
            break;
        default:
            break;
    }
}

void HTMLInputElement::select(  )
{
    if (!renderer())
        return;

    switch (m_type) {
        case FILE:
            static_cast<RenderFileButton*>(renderer())->select();
            break;
        case TEXT:
            static_cast<RenderTextField *>(renderer())->select();
            break;
        case PASSWORD:
        case SEARCH:
            static_cast<RenderLineEdit*>(renderer())->select();
            break;
        case BUTTON:
        case CHECKBOX:
        case HIDDEN:
        case IMAGE:
        case ISINDEX:
        case RADIO:
        case RANGE:
        case RESET:
        case SUBMIT:
            break;
    }
}

void HTMLInputElement::setSelectionRange(int start, int end)
{
    if (!renderer())
        return;
    
    switch (m_type) {
        case TEXT:
            static_cast<RenderTextField *>(renderer())->setSelectionRange(start, end);
            break;
        case PASSWORD:
        case SEARCH:
            static_cast<RenderLineEdit *>(renderer())->setSelectionRange(start, end);
            break;
        default:
            break;
    }
}

void HTMLInputElement::click(bool sendMouseEvents, bool showPressedLook)
{
    switch (inputType()) {
        case HIDDEN:
            // a no-op for this type
            break;
        case SUBMIT:
        case RESET:
        case BUTTON:
            HTMLGenericFormElement::click(sendMouseEvents, showPressedLook);
            break;
        case FILE:
            if (renderer()) {
                static_cast<RenderFileButton *>(renderer())->click(sendMouseEvents);
                break;
            }
            HTMLGenericFormElement::click(sendMouseEvents, showPressedLook);
            break;
        case CHECKBOX:
        case RADIO:
        case IMAGE:
        case ISINDEX:
        case PASSWORD:
        case SEARCH:
        case RANGE:
        case TEXT:
            HTMLGenericFormElement::click(sendMouseEvents, showPressedLook);
            break;
    }
}

void HTMLInputElement::accessKeyAction(bool sendToAnyElement)
{
    switch (inputType()) {
        case HIDDEN:
            // a no-op for this type
            break;
        case TEXT:
        case PASSWORD:
        case SEARCH:
        case ISINDEX:
            focus();
            break;
        case CHECKBOX:
        case RADIO:
        case SUBMIT:
        case RESET:
        case IMAGE:
        case BUTTON:
        case FILE:
        case RANGE:
            // focus
            focus();

            // send the mouse button events iff the
            // caller specified sendToAnyElement
            click(sendToAnyElement);
            break;
    }
}

bool HTMLInputElement::mapToEntry(const QualifiedName& attrName, MappedAttributeEntry& result) const
{
    if (attrName == widthAttr ||
        attrName == heightAttr ||
        attrName == vspaceAttr ||
        attrName == hspaceAttr) {
        result = eUniversal;
        return false;
    } 
    
    if (attrName == alignAttr) {
        result = eReplaced; // Share with <img> since the alignment behavior is the same.
        return false;
    }
    
    return HTMLElement::mapToEntry(attrName, result);
}

void HTMLInputElement::parseMappedAttribute(MappedAttribute *attr)
{
    if (attr->name() == nameAttr) {
        if (m_type == RADIO && checked()) {
            // Remove the radio from its old group.
            if (m_type == RADIO && !m_name.isEmpty())
                document()->removeRadioButtonGroup(m_name.impl(), m_form);
        }
        
        // Update our cached reference to the name.
        m_name = attr->value();
        
        if (m_type == RADIO) {
            // In case we parsed the checked attribute first, call setChecked if the element is checked by default.
            if (m_useDefaultChecked)
                setChecked(m_defaultChecked);
            // Add the button to its new group.
            if (checked())
                document()->radioButtonChecked(this, m_form);
        }
        
        
    } else if (attr->name() == autocompleteAttr) {
        m_autocomplete = !equalIgnoringCase(attr->value(), "off");
    } else if (attr->name() ==  typeAttr) {
        setInputType(attr->value());
    } else if (attr->name() == valueAttr) {
        // We only need to setChanged if the form is looking at the default value right now.
        if (m_value.isNull())
            setChanged();
        m_valueMatchesRenderer = false;
    } else if (attr->name() == checkedAttr) {
        m_defaultChecked = !attr->isNull();
        if (m_useDefaultChecked) {
            setChecked(m_defaultChecked);
            m_useDefaultChecked = true;
        }
    } else if (attr->name() == maxlengthAttr) {
        m_maxLen = !attr->isNull() ? attr->value().toInt() : 1024;
        if (m_maxLen <= 0 || m_maxLen > 1024)
            m_maxLen = 1024;
        setChanged();
    } else if (attr->name() == sizeAttr) {
        m_size = !attr->isNull() ? attr->value().toInt() : 20;
    } else if (attr->name() == altAttr) {
        if (renderer() && m_type == IMAGE)
            static_cast<RenderImage*>(renderer())->updateAltText();
    } else if (attr->name() == srcAttr) {
        if (renderer() && m_type == IMAGE) {
            if (!m_imageLoader)
                m_imageLoader = new HTMLImageLoader(this);
            m_imageLoader->updateFromElement();
        }
    } else if (attr->name() == usemapAttr ||
               attr->name() == accesskeyAttr) {
        // FIXME: ignore for the moment
    } else if (attr->name() == vspaceAttr) {
        addCSSLength(attr, CSS_PROP_MARGIN_TOP, attr->value());
        addCSSLength(attr, CSS_PROP_MARGIN_BOTTOM, attr->value());
    } else if (attr->name() == hspaceAttr) {
        addCSSLength(attr, CSS_PROP_MARGIN_LEFT, attr->value());
        addCSSLength(attr, CSS_PROP_MARGIN_RIGHT, attr->value());
    } else if (attr->name() == alignAttr) {
        addHTMLAlignment(attr);
    } else if (attr->name() == widthAttr) {
        addCSSLength(attr, CSS_PROP_WIDTH, attr->value());
    } else if (attr->name() == heightAttr) {
        addCSSLength(attr, CSS_PROP_HEIGHT, attr->value());
    } else if (attr->name() == onfocusAttr) {
        setHTMLEventListener(focusEvent, attr);
    } else if (attr->name() == onblurAttr) {
        setHTMLEventListener(blurEvent, attr);
    } else if (attr->name() == onselectAttr) {
        setHTMLEventListener(selectEvent, attr);
    } else if (attr->name() == onchangeAttr) {
        setHTMLEventListener(changeEvent, attr);
    } else if (attr->name() == oninputAttr) {
        setHTMLEventListener(inputEvent, attr);
    }
    // Search field and slider attributes all just cause updateFromElement to be called through style
    // recalcing.
    else if (attr->name() == onsearchAttr) {
        setHTMLEventListener(searchEvent, attr);
    } else if (attr->name() == resultsAttr) {
        m_maxResults = !attr->isNull() ? attr->value().toInt() : -1;
        setChanged();
    } else if (attr->name() == autosaveAttr ||
               attr->name() == incrementalAttr ||
               attr->name() == placeholderAttr ||
               attr->name() == minAttr ||
               attr->name() == maxAttr ||
               attr->name() == precisionAttr) {
        setChanged();
    } else
        HTMLGenericFormElement::parseMappedAttribute(attr);
}

bool HTMLInputElement::rendererIsNeeded(RenderStyle *style)
{
    switch(m_type)
    {
    case TEXT:
    case PASSWORD:
    case SEARCH:
    case RANGE:
    case ISINDEX:
    case CHECKBOX:
    case RADIO:
    case SUBMIT:
    case IMAGE:
    case RESET:
    case FILE:
    case BUTTON:   return HTMLGenericFormElement::rendererIsNeeded(style);
    case HIDDEN:   return false;
    }
    assert(false);
    return false;
}

RenderObject *HTMLInputElement::createRenderer(RenderArena *arena, RenderStyle *style)
{
    switch(m_type)
    {
    case TEXT:     return new (arena) RenderTextField(this);
    case PASSWORD:
    case SEARCH:
    case ISINDEX:  return new (arena) RenderLineEdit(this);
    case CHECKBOX:
    case RADIO:
        return RenderObject::createObject(this, style);
    case SUBMIT:
    case RESET:
    case BUTTON:
        return new (arena) RenderButton(this);
    case IMAGE:    return new (arena) RenderImageButton(this);
    case FILE:     return new (arena) RenderFileButton(this);
    case RANGE:    return new (arena) RenderSlider(this);
    case HIDDEN:   break;
    }
    assert(false);
    return 0;
}

void HTMLInputElement::attach()
{
    if (!m_inited) {
        if (!m_haveType)
            setInputType(getAttribute(typeAttr));

        // FIXME: This needs to be dynamic, doesn't it, since someone could set this
        // after attachment?
        String val = getAttribute(valueAttr);
        if ((unsigned) m_type <= ISINDEX && !val.isEmpty()) {
            // remove newline stuff..
            DeprecatedString nvalue;
            for (unsigned int i = 0; i < val.length(); ++i)
                if (val[i] >= ' ')
                    nvalue += val[i];

            if (val.length() != nvalue.length())
                setAttribute(valueAttr, nvalue);
        }

        m_inited = true;
    }

    // Disallow the width attribute on inputs other than HIDDEN and IMAGE.
    // Dumb Web sites will try to set the width as an attribute on form controls that aren't
    // images or hidden.
    if (hasMappedAttributes() && m_type != HIDDEN && m_type != IMAGE && !getAttribute(widthAttr).isEmpty()) {
        int excCode;
        removeAttribute(widthAttr, excCode);
    }

    HTMLGenericFormElement::attach();

    if (m_type == IMAGE) {
        if (!m_imageLoader)
            m_imageLoader = new HTMLImageLoader(this);
        m_imageLoader->updateFromElement();
        if (renderer()) {
            RenderImage* imageObj = static_cast<RenderImage*>(renderer());
            imageObj->setCachedImage(m_imageLoader->image());    
        }
    }

    // note we don't deal with calling passwordFieldRemoved() on detach, because the timing
    // was such that it cleared our state too early
    if (m_type == PASSWORD)
        document()->passwordFieldAdded();
}

void HTMLInputElement::detach()
{
    HTMLGenericFormElement::detach();
    m_valueMatchesRenderer = false;
}

String HTMLInputElement::altText() const
{
    // http://www.w3.org/TR/1998/REC-html40-19980424/appendix/notes.html#altgen
    // also heavily discussed by Hixie on bugzilla
    // note this is intentionally different to HTMLImageElement::altText()
    String alt = getAttribute(altAttr);
    // fall back to title attribute
    if (alt.isNull())
        alt = getAttribute(titleAttr);
    if (alt.isNull())
        alt = getAttribute(valueAttr);
    if (alt.isEmpty())
        alt = inputElementAltText();

    return alt;
}

bool HTMLInputElement::isSuccessfulSubmitButton() const
{
    // HTML spec says that buttons must have names
    // to be considered successful. However, other browsers
    // do not impose this constraint. Therefore, we behave
    // differently and can use different buttons than the 
    // author intended. 
    // Was: (m_type == SUBMIT && !name().isEmpty())
    return !m_disabled && (m_type == IMAGE || m_type == SUBMIT);
}

bool HTMLInputElement::isActivatedSubmit() const
{
    return m_activeSubmit;
}

void HTMLInputElement::setActivatedSubmit(bool flag)
{
    m_activeSubmit = flag;
}

bool HTMLInputElement::appendFormData(FormDataList &encoding, bool multipart)
{
    // image generates its own names
    if (name().isEmpty() && m_type != IMAGE) return false;

    switch (m_type) {
        case HIDDEN:
        case TEXT:
        case SEARCH:
        case RANGE:
        case PASSWORD:
            // always successful
            encoding.appendData(name(), value());
            return true;

        case CHECKBOX:
        case RADIO:
            if (checked()) {
                encoding.appendData(name(), value());
                return true;
            }
            break;

        case BUTTON:
        case RESET:
            // those buttons are never successful
            return false;

        case IMAGE:
            if (m_activeSubmit) {
                encoding.appendData(name().isEmpty() ? "x" : (name() + ".x"), clickX());
                encoding.appendData(name().isEmpty() ? "y" : (name() + ".y"), clickY());
                if (!name().isEmpty() && !value().isEmpty())
                    encoding.appendData(name(), value());
                return true;
            }
            break;

        case SUBMIT:
            if (m_activeSubmit) {
                String enc_str = valueWithDefault();
                if (!enc_str.isEmpty()) {
                    encoding.appendData(name(), enc_str);
                    return true;
                }
            }
            break;

        case FILE:
        {
            // can't submit file on GET
            // don't submit if display: none or display: hidden
            if(!multipart || !renderer() || renderer()->style()->visibility() != WebCore::VISIBLE)
                return false;

            // if no filename at all is entered, return successful, however empty
            // null would be more logical but netscape posts an empty file. argh.
            if (value().isEmpty()) {
                encoding.appendData(name(), DeprecatedString(""));
                return true;
            }

            encoding.appendFile(name(), value());
            return true;
        }
        case ISINDEX:
            encoding.appendData(name(), value());
            return true;
    }
    return false;
}

void HTMLInputElement::reset()
{
    if (storesValueSeparateFromAttribute())
        setValue(String());
    setChecked(m_defaultChecked);
    m_useDefaultChecked = true;
}

void HTMLInputElement::setChecked(bool nowChecked)
{
    // We mimic WinIE and don't allow unnamed radio buttons to be checked.
    if (checked() == nowChecked || (m_type == RADIO && name().isEmpty()))
        return;

    if (m_type == RADIO && nowChecked)
        document()->radioButtonChecked(this, m_form);

    m_useDefaultChecked = false;
    m_checked = nowChecked;
    setChanged();
    if (renderer() && renderer()->style()->hasAppearance())
        theme()->stateChanged(renderer(), CheckedState);

    // Only send a change event for items in the document (avoid firing during
    // parsing) and don't send a change event for a radio button that's getting
    // unchecked to match other browsers. DOM is not a useful standard for this
    // because it says only to fire change events at "lose focus" time, which is
    // definitely wrong in practice for these types of elements.
    if (inDocument() && (m_type != RADIO || nowChecked))
        onChange();
}

void HTMLInputElement::setIndeterminate(bool _indeterminate)
{
    // Only checkboxes honor indeterminate.
    if (m_type != CHECKBOX || indeterminate() == _indeterminate)
        return;

    m_indeterminate = _indeterminate;

    setChanged();

    if (renderer() && renderer()->style()->hasAppearance())
        theme()->stateChanged(renderer(), CheckedState);
}

void HTMLInputElement::copyNonAttributeProperties(const Element *source)
{
    const HTMLInputElement *sourceElem = static_cast<const HTMLInputElement *>(source);

    m_value = sourceElem->m_value;
    m_checked = sourceElem->m_checked;
    m_indeterminate = sourceElem->m_indeterminate;
}

String HTMLInputElement::value() const
{
    String value = m_value;

    // It's important *not* to fall back to the value attribute for file inputs,
    // because that would allow a malicious web page to upload files by setting the
    // value attribute in markup.
    if (value.isNull() && m_type != FILE)
        value = getAttribute(valueAttr);

    // If no attribute exists, then just use "on" or "" based off the checked() state of the control.
    if (value.isNull() && (m_type == CHECKBOX || m_type == RADIO))
        return String(checked() ? "on" : "");

    return value;
}

String HTMLInputElement::valueWithDefault() const
{
    String v = value();
    if (v.isNull()) {
        switch (m_type) {
            case RESET:
                v = resetButtonDefaultLabel();
                break;

            case SUBMIT:
                v = submitButtonDefaultLabel();
                break;

            case BUTTON:
            case CHECKBOX:
            case FILE:
            case HIDDEN:
            case IMAGE:
            case ISINDEX:
            case PASSWORD:
            case RADIO:
            case RANGE:
            case SEARCH:
            case TEXT:
                break;
        }
    }
    return v;
}

void HTMLInputElement::setValue(const String &value)
{
    if (m_type == FILE) return;

    m_valueMatchesRenderer = false;
    if (storesValueSeparateFromAttribute()) {
        m_value = value;
        if (renderer())
            renderer()->updateFromElement();
        setChanged();
    } else {
        setAttribute(valueAttr, value);
    }
}

void HTMLInputElement::setValueFromRenderer(const String &value)
{
    m_value = value;
    m_valueMatchesRenderer = true;
    
    // Fire the "input" DOM event.
    dispatchHTMLEvent(inputEvent, true, false);
}

bool HTMLInputElement::storesValueSeparateFromAttribute() const
{
    switch (m_type) {
        case BUTTON:
        case CHECKBOX:
        case FILE:
        case HIDDEN:
        case IMAGE:
        case RADIO:
        case RANGE:
        case RESET:
        case SUBMIT:
            return false;
        case ISINDEX:
        case PASSWORD:
        case SEARCH:
        case TEXT:
            return true;
    }
    return false;
}

void* HTMLInputElement::preDispatchEventHandler(Event *evt)
{
    // preventDefault or "return false" are used to reverse the automatic checking/selection we do here.
    // This result gives us enough info to perform the "undo" in postDispatch of the action we take here.
    void* result = 0; 
    if ((m_type == CHECKBOX || m_type == RADIO) && evt->isMouseEvent() && evt->type() == clickEvent && 
        static_cast<MouseEvent*>(evt)->button() == 0) {
        if (m_type == CHECKBOX) {
            // As a way to store the state, we return 0 if we were unchecked, 1 if we were checked, and 2 for
            // indeterminate.
            if (indeterminate()) {
                result = (void*)0x2;
                setIndeterminate(false);
            } else {
                if (checked())
                    result = (void*)0x1;
                setChecked(!checked());
            }
        } else {
            // For radio buttons, store the current selected radio object.
            if (name().isEmpty() || checked())
                return 0; // Unnamed radio buttons dont get checked. Checked buttons just stay checked.
                          // FIXME: Need to learn to work without a form.

            // We really want radio groups to end up in sane states, i.e., to have something checked.
            // Therefore if nothing is currently selected, we won't allow this action to be "undone", since
            // we want some object in the radio group to actually get selected.
            HTMLInputElement* currRadio = document()->checkedRadioButtonForGroup(name().impl(), m_form);
            if (currRadio) {
                // We have a radio button selected that is not us.  Cache it in our result field and ref it so
                // that it can't be destroyed.
                currRadio->ref();
                result = currRadio;
            }
            setChecked(true);
        }
    }
    return result;
}

void HTMLInputElement::postDispatchEventHandler(Event *evt, void* data)
{
    if ((m_type == CHECKBOX || m_type == RADIO) && evt->isMouseEvent() && evt->type() == clickEvent && 
        static_cast<MouseEvent*>(evt)->button() == 0) {
        if (m_type == CHECKBOX) {
            // Reverse the checking we did in preDispatch.
            if (evt->defaultPrevented() || evt->defaultHandled()) {
                if (data == (void*)0x2)
                    setIndeterminate(true);
                else
                    setChecked(data);
            }
        } else if (data) {
            HTMLInputElement* input = static_cast<HTMLInputElement*>(data);
            if (evt->defaultPrevented() || evt->defaultHandled()) {
                // Restore the original selected radio button if possible.
                // Make sure it is still a radio button and only do the restoration if it still
                // belongs to our group.
                if (input->form() == form() && input->inputType() == RADIO && input->name() == name()) {
                    // Ok, the old radio button is still in our form and in our group and is still a 
                    // radio button, so it's safe to restore selection to it.
                    input->setChecked(true);
                }
            }
            input->deref();
        }
    }
}

void HTMLInputElement::defaultEventHandler(Event *evt)
{
    if (m_type == IMAGE && evt->isMouseEvent() && evt->type() == clickEvent) {
        // record the mouse position for when we get the DOMActivate event
        MouseEvent *me = static_cast<MouseEvent*>(evt);
        // FIXME: We could just call offsetX() and offsetY() on the event,
        // but that's currently broken, so for now do the computation here.
        if (me->isSimulated() || !renderer()) {
            xPos = 0;
            yPos = 0;
        } else {
            int offsetX, offsetY;
            renderer()->absolutePosition(offsetX, offsetY);
            xPos = me->clientX() - offsetX;
            yPos = me->clientY() - offsetY;
        }
        me->setDefaultHandled();
    }

    // DOMActivate events cause the input to be "activated" - in the case of image and submit inputs, this means
    // actually submitting the form. For reset inputs, the form is reset. These events are sent when the user clicks
    // on the element, or presses enter while it is the active element. Javacsript code wishing to activate the element
    // must dispatch a DOMActivate event - a click event will not do the job.
    if (evt->type() == DOMActivateEvent) {
        if (m_type == IMAGE || m_type == SUBMIT || m_type == RESET) {
            if (!m_form)
                return;
            if (m_type == RESET)
                m_form->reset();
            else {
                m_activeSubmit = true;
                if (!m_form->prepareSubmit()) {
                    xPos = 0;
                    yPos = 0;
                }
                m_activeSubmit = false;
            }
        } 
    }

    // Use key press event here since sending simulated mouse events
    // on key down blocks the proper sending of the key press event.
    if (evt->type() == keypressEvent && evt->isKeyboardEvent()) {
        bool clickElement = false;
        bool clickDefaultFormButton = false;

        String key = static_cast<KeyboardEvent *>(evt)->keyIdentifier();

        if (key == "U+000020") {
            switch (m_type) {
                case BUTTON:
                case CHECKBOX:
                case FILE:
                case IMAGE:
                case RESET:
                case SUBMIT:
                    // Simulate mouse click for spacebar for these types of elements.
                    // The AppKit already does this for some, but not all, of them.
                    clickElement = true;
                    break;
                case RADIO:
                    // If an unselected radio is tabbed into (because the entire group has nothing
                    // checked, or because of some explicit .focus() call), then allow space to check it.
                    if (!checked())
                        clickElement = true;
                    break;
                case HIDDEN:
                case ISINDEX:
                case PASSWORD:
                case RANGE:
                case SEARCH:
                case TEXT:
                    break;
            }
        }

        if (key == "Enter") {
            switch (m_type) {
                case BUTTON:
                case CHECKBOX:
                case HIDDEN:
                case ISINDEX:
                case PASSWORD:
                case RANGE:
                case SEARCH:
                case TEXT:
                    // Simulate mouse click on the default form button for enter for these types of elements.
                    clickDefaultFormButton = true;
                    break;
                case FILE:
                case IMAGE:
                case RESET:
                case SUBMIT:
                    // Simulate mouse click for enter for these types of elements.
                    clickElement = true;
                    break;
                case RADIO:
                    break; // Don't do anything for enter on a radio button.
            }
        }

        if (m_type == RADIO && (key == "Up" || key == "Down" || key == "Left" || key == "Right")) {
            // Left and up mean "previous radio button".
            // Right and down mean "next radio button".
            // Tested in WinIE, and even for RTL, left still means previous radio button (and so moves
            // to the right).  Seems strange, but we'll match it.
            bool forward = (key == "Down" || key == "Right");
            
            // We can only stay within the form's children if the form hasn't been demoted to a leaf because
            // of malformed HTML.
            Node* n = this;
            while ((n = (forward ? n->traverseNextNode() : n->traversePreviousNode()))) {
                // Once we encounter a form element, we know we're through.
                if (n->hasTagName(formTag))
                    break;
                    
                // Look for more radio buttons.
                if (n->hasTagName(inputTag)) {
                    HTMLInputElement* elt = static_cast<HTMLInputElement*>(n);
                    if (elt->form() != m_form)
                        break;
                    if (n->hasTagName(inputTag)) {
                        HTMLInputElement* inputElt = static_cast<HTMLInputElement*>(n);
                        if (inputElt->inputType() == RADIO && inputElt->name() == name() &&
                            inputElt->isFocusable()) {
                            inputElt->setChecked(true);
                            document()->setFocusNode(inputElt);
                            inputElt->click(false, false);
                            evt->setDefaultHandled();
                            break;
                        }
                    }
                }
            }
        }

        if (clickElement) {
            click(false);
            evt->setDefaultHandled();
        } else if (clickDefaultFormButton && m_form) {
            m_form->submitClick();
            evt->setDefaultHandled();
        }
    }

    if (evt->isBeforeTextInsertedEvent()) {
        // Make sure that the text to be inserted will not violate the maxLength
        unsigned ml = maxLength();
        unsigned currentLength = value().length();
        String text = static_cast<BeforeTextInsertedEvent *>(evt)->text();
        int selectionLength = document()->frame()->selection().toString().length();
        
        // Truncate the inserted text if necessary
        if (currentLength + text.length() - selectionLength > ml) {
            ASSERT(currentLength <= ml);
            text.truncate(ml - currentLength);
        }        
    }
    
    if (m_type == TEXT && (evt->isMouseEvent() || evt->isDragEvent() || evt->isWheelEvent()) && renderer())
        static_cast<RenderTextField*>(renderer())->forwardEvent(evt);
    
    HTMLGenericFormElement::defaultEventHandler(evt);
}

bool HTMLInputElement::isEditable()
{
    return ((m_type == TEXT) || (m_type == PASSWORD) ||
            (m_type == SEARCH) || (m_type == ISINDEX) || (m_type == FILE));
}

bool HTMLInputElement::isURLAttribute(Attribute *attr) const
{
    return (attr->name() == srcAttr);
}

String HTMLInputElement::defaultValue() const
{
    return getAttribute(valueAttr);
}

void HTMLInputElement::setDefaultValue(const String &value)
{
    setAttribute(valueAttr, value);
}

bool HTMLInputElement::defaultChecked() const
{
    return !getAttribute(checkedAttr).isNull();
}

void HTMLInputElement::setDefaultChecked(bool defaultChecked)
{
    setAttribute(checkedAttr, defaultChecked ? "" : 0);
}

String HTMLInputElement::accept() const
{
    return getAttribute(acceptAttr);
}

void HTMLInputElement::setAccept(const String &value)
{
    setAttribute(acceptAttr, value);
}

String HTMLInputElement::accessKey() const
{
    return getAttribute(accesskeyAttr);
}

void HTMLInputElement::setAccessKey(const String &value)
{
    setAttribute(accesskeyAttr, value);
}

String HTMLInputElement::align() const
{
    return getAttribute(alignAttr);
}

void HTMLInputElement::setAlign(const String &value)
{
    setAttribute(alignAttr, value);
}

String HTMLInputElement::alt() const
{
    return getAttribute(altAttr);
}

void HTMLInputElement::setAlt(const String &value)
{
    setAttribute(altAttr, value);
}

void HTMLInputElement::setMaxLength(int _maxLength)
{
    setAttribute(maxlengthAttr, String::number(_maxLength));
}

void HTMLInputElement::setSize(unsigned _size)
{
    setAttribute(sizeAttr, String::number(_size));
}

String HTMLInputElement::src() const
{
    return document()->completeURL(getAttribute(srcAttr));
}

void HTMLInputElement::setSrc(const String &value)
{
    setAttribute(srcAttr, value);
}

String HTMLInputElement::useMap() const
{
    return getAttribute(usemapAttr);
}

void HTMLInputElement::setUseMap(const String &value)
{
    setAttribute(usemapAttr, value);
}
    
} // namespace
