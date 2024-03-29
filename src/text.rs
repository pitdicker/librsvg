//! Text elements: `text`, `tspan`, `tref`.

use markup5ever::{expanded_name, local_name, namespace_url, ns};
use std::cell::RefCell;
use std::rc::Rc;

use crate::bbox::BoundingBox;
use crate::document::{AcquiredNodes, NodeId};
use crate::drawing_ctx::DrawingCtx;
use crate::element::{Draw, Element, ElementResult, SetAttributes};
use crate::error::*;
use crate::layout::{self, FontProperties, StackingContext, Stroke};
use crate::length::*;
use crate::node::{CascadedValues, Node, NodeBorrow};
use crate::parsers::ParseValue;
use crate::properties::{
    ComputedValues, Direction, FontStretch, FontStyle, FontVariant, FontWeight, TextAnchor,
    UnicodeBidi, WritingMode, XmlLang, XmlSpace,
};
use crate::space::{xml_space_normalize, NormalizeDefault, XmlSpaceNormalize};
use crate::xml::Attributes;

/// An absolutely-positioned array of `Span`s
///
/// SVG defines a "[text chunk]" to occur when a text-related element
/// has an absolute position adjustment, that is, `x` or `y`
/// attributes.
///
/// A `<text>` element always starts with an absolute position from
/// such attributes, or (0, 0) if they are not specified.
///
/// Subsequent children of the `<text>` element will create new chunks
/// whenever they have `x` or `y` attributes.
///
/// [text chunk]: https://www.w3.org/TR/SVG11/text.html#TextLayoutIntroduction
struct Chunk {
    values: Rc<ComputedValues>,
    x: Option<f64>,
    y: Option<f64>,
    spans: Vec<Span>,
}

struct MeasuredChunk {
    values: Rc<ComputedValues>,
    x: Option<f64>,
    y: Option<f64>,
    advance: (f64, f64),
    spans: Vec<MeasuredSpan>,
}

struct PositionedChunk {
    next_chunk_x: f64,
    next_chunk_y: f64,
    spans: Vec<PositionedSpan>,
}

struct Span {
    values: Rc<ComputedValues>,
    text: String,
    dx: f64,
    dy: f64,
    _depth: usize,
}

struct MeasuredSpan {
    values: Rc<ComputedValues>,
    layout: pango::Layout,
    _layout_size: (f64, f64),
    advance: (f64, f64),
    dx: f64,
    dy: f64,
}

struct PositionedSpan {
    layout: pango::Layout,
    values: Rc<ComputedValues>,
    _position: (f64, f64),
    rendered_position: (f64, f64),
    next_span_x: f64,
    next_span_y: f64,
}

impl Chunk {
    fn new(values: &ComputedValues, x: Option<f64>, y: Option<f64>) -> Chunk {
        Chunk {
            values: Rc::new(values.clone()),
            x,
            y,
            spans: Vec::new(),
        }
    }
}

impl MeasuredChunk {
    fn from_chunk(chunk: &Chunk, draw_ctx: &DrawingCtx) -> MeasuredChunk {
        let measured_spans: Vec<MeasuredSpan> = chunk
            .spans
            .iter()
            .map(|span| MeasuredSpan::from_span(span, draw_ctx))
            .collect();

        let advance = measured_spans.iter().fold((0.0, 0.0), |acc, measured| {
            (acc.0 + measured.advance.0, acc.1 + measured.advance.1)
        });

        MeasuredChunk {
            values: chunk.values.clone(),
            x: chunk.x,
            y: chunk.y,
            advance,
            spans: measured_spans,
        }
    }
}

impl PositionedChunk {
    fn from_measured(
        measured: &MeasuredChunk,
        draw_ctx: &DrawingCtx,
        x: f64,
        y: f64,
    ) -> PositionedChunk {
        let mut positioned = Vec::new();

        // Adjust the specified coordinates with the text_anchor

        let adjusted_advance = text_anchor_advance(
            measured.values.text_anchor(),
            measured.values.writing_mode(),
            measured.advance,
        );

        let mut x = x + adjusted_advance.0;
        let mut y = y + adjusted_advance.1;

        // Position each span

        for measured_span in &measured.spans {
            let positioned_span = PositionedSpan::from_measured(measured_span, draw_ctx, x, y);

            x = positioned_span.next_span_x;
            y = positioned_span.next_span_y;

            positioned.push(positioned_span);
        }

        PositionedChunk {
            next_chunk_x: x,
            next_chunk_y: y,
            spans: positioned,
        }
    }
}

fn text_anchor_advance(
    anchor: TextAnchor,
    writing_mode: WritingMode,
    advance: (f64, f64),
) -> (f64, f64) {
    if writing_mode.is_vertical() {
        match anchor {
            TextAnchor::Start => (0.0, 0.0),
            TextAnchor::Middle => (0.0, -advance.1 / 2.0),
            TextAnchor::End => (0.0, -advance.1),
        }
    } else {
        match anchor {
            TextAnchor::Start => (0.0, 0.0),
            TextAnchor::Middle => (-advance.0 / 2.0, 0.0),
            TextAnchor::End => (-advance.0, 0.0),
        }
    }
}

impl Span {
    fn new(text: &str, values: Rc<ComputedValues>, dx: f64, dy: f64, depth: usize) -> Span {
        Span {
            values,
            text: text.to_string(),
            dx,
            dy,
            _depth: depth,
        }
    }
}

impl MeasuredSpan {
    fn from_span(span: &Span, draw_ctx: &DrawingCtx) -> MeasuredSpan {
        let values = span.values.clone();

        let view_params = draw_ctx.get_view_params();
        let params = NormalizeParams::new(&values, &view_params);

        let properties = FontProperties::new(&values, &params);
        let layout = create_pango_layout(draw_ctx, &properties, &span.text);
        let (w, h) = layout.size();

        let w = f64::from(w) / f64::from(pango::SCALE);
        let h = f64::from(h) / f64::from(pango::SCALE);

        let advance = if values.writing_mode().is_vertical() {
            (0.0, w)
        } else {
            (w, 0.0)
        };

        MeasuredSpan {
            values,
            layout,
            _layout_size: (w, h),
            advance,
            dx: span.dx,
            dy: span.dy,
        }
    }
}

impl PositionedSpan {
    fn from_measured(
        measured: &MeasuredSpan,
        draw_ctx: &DrawingCtx,
        x: f64,
        y: f64,
    ) -> PositionedSpan {
        let layout = measured.layout.clone();
        let values = measured.values.clone();

        let view_params = draw_ctx.get_view_params();
        let params = NormalizeParams::new(&values, &view_params);

        let baseline = f64::from(layout.baseline()) / f64::from(pango::SCALE);
        let baseline_shift = values.baseline_shift().0.to_user(&params);
        let offset = baseline + baseline_shift;

        let dx = measured.dx;
        let dy = measured.dy;

        let (render_x, render_y) = if values.writing_mode().is_vertical() {
            (x + offset + dx, y + dy)
        } else {
            (x + dx, y - offset + dy)
        };

        PositionedSpan {
            layout: measured.layout.clone(),
            values,
            _position: (x, y),
            rendered_position: (render_x, render_y),
            next_span_x: x + measured.advance.0 + dx,
            next_span_y: y + measured.advance.1 + dy,
        }
    }

    fn draw(
        &self,
        acquired_nodes: &mut AcquiredNodes<'_>,
        draw_ctx: &mut DrawingCtx,
        clipping: bool,
    ) -> Result<BoundingBox, RenderingError> {
        let view_params = draw_ctx.get_view_params();
        let params = NormalizeParams::new(&self.values, &view_params);

        let layout = self.layout.clone();
        let is_visible = self.values.is_visible();
        let (x, y) = self.rendered_position;

        let stroke = Stroke::new(&self.values, &params);

        let stroke_paint = self.values.stroke().0.resolve(
            acquired_nodes,
            self.values.stroke_opacity().0,
            self.values.color().0,
            None,
            None,
        );

        let fill_paint = self.values.fill().0.resolve(
            acquired_nodes,
            self.values.fill_opacity().0,
            self.values.color().0,
            None,
            None,
        );

        let text_rendering = self.values.text_rendering();

        let span = layout::TextSpan {
            layout,
            is_visible,
            x,
            y,
            stroke,
            stroke_paint,
            fill_paint,
            text_rendering,
        };

        draw_ctx.draw_text_span(&view_params, &span, acquired_nodes, &self.values, clipping)
    }
}

/// Walks the children of a `<text>`, `<tspan>`, or `<tref>` element
/// and appends chunks/spans from them into the specified `chunks`
/// array.
///
/// `x` and `y` are the absolute position for the first chunk.  If the
/// first child is a `<tspan>` with a specified absolute position, it
/// will be used instead of the given arguments.
fn children_to_chunks(
    chunks: &mut Vec<Chunk>,
    node: &Node,
    acquired_nodes: &mut AcquiredNodes<'_>,
    cascaded: &CascadedValues<'_>,
    draw_ctx: &mut DrawingCtx,
    dx: f64,
    dy: f64,
    depth: usize,
) {
    for child in node.children() {
        if child.is_chars() {
            let values = cascaded.get();
            child
                .borrow_chars()
                .to_chunks(&child, Rc::new(values.clone()), chunks, dx, dy, depth);
        } else {
            assert!(child.is_element());

            match *child.borrow_element() {
                Element::TSpan(ref tspan) => {
                    let cascaded = CascadedValues::new(cascaded, &child);
                    tspan.to_chunks(
                        &child,
                        acquired_nodes,
                        &cascaded,
                        draw_ctx,
                        chunks,
                        dx,
                        dy,
                        depth + 1,
                    );
                }

                Element::TRef(ref tref) => {
                    let cascaded = CascadedValues::new(cascaded, &child);
                    tref.to_chunks(&child, acquired_nodes, &cascaded, chunks, depth + 1);
                }

                _ => (),
            }
        }
    }
}

/// In SVG text elements, we use `Chars` to store character data.  For example,
/// an element like `<text>Foo Bar</text>` will be a `Text` with a single child,
/// and the child will be a `Chars` with "Foo Bar" for its contents.
///
/// Text elements can contain `<tspan>` sub-elements.  In this case,
/// those `tspan` nodes will also contain `Chars` children.
///
/// A text or tspan element can contain more than one `Chars` child, for example,
/// if there is an XML comment that splits the character contents in two:
///
/// ```xml
/// <text>
///   This sentence will create a Chars.
///   <!-- this comment is ignored -->
///   This sentence will cretea another Chars.
/// </text>
/// ```
///
/// When rendering a text element, it will take care of concatenating the strings
/// in its `Chars` children as appropriate, depending on the
/// `xml:space="preserve"` attribute.  A `Chars` stores the characters verbatim
/// as they come out of the XML parser, after ensuring that they are valid UTF-8.

#[derive(Default)]
pub struct Chars {
    string: RefCell<String>,
    space_normalized: RefCell<Option<String>>,
}

impl Chars {
    pub fn new(initial_text: &str) -> Chars {
        Chars {
            string: RefCell::new(String::from(initial_text)),
            space_normalized: RefCell::new(None),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.string.borrow().is_empty()
    }

    pub fn append(&self, s: &str) {
        self.string.borrow_mut().push_str(s);
        *self.space_normalized.borrow_mut() = None;
    }

    fn ensure_normalized_string(&self, node: &Node, values: &ComputedValues) {
        let mut normalized = self.space_normalized.borrow_mut();

        if (*normalized).is_none() {
            let mode = match values.xml_space() {
                XmlSpace::Default => XmlSpaceNormalize::Default(NormalizeDefault {
                    has_element_before: node.previous_sibling().is_some(),
                    has_element_after: node.next_sibling().is_some(),
                }),

                XmlSpace::Preserve => XmlSpaceNormalize::Preserve,
            };

            *normalized = Some(xml_space_normalize(mode, &self.string.borrow()));
        }
    }

    fn make_span(
        &self,
        node: &Node,
        values: Rc<ComputedValues>,
        dx: f64,
        dy: f64,
        depth: usize,
    ) -> Option<Span> {
        self.ensure_normalized_string(node, &*values);

        if self.space_normalized.borrow().as_ref().unwrap() == "" {
            None
        } else {
            Some(Span::new(
                self.space_normalized.borrow().as_ref().unwrap(),
                values,
                dx,
                dy,
                depth,
            ))
        }
    }

    fn to_chunks(
        &self,
        node: &Node,
        values: Rc<ComputedValues>,
        chunks: &mut Vec<Chunk>,
        dx: f64,
        dy: f64,
        depth: usize,
    ) {
        if let Some(span) = self.make_span(&node, values, dx, dy, depth) {
            let num_chunks = chunks.len();
            assert!(num_chunks > 0);

            chunks[num_chunks - 1].spans.push(span);
        }
    }

    pub fn get_string(&self) -> String {
        self.string.borrow().clone()
    }
}

#[derive(Default)]
pub struct Text {
    x: Length<Horizontal>,
    y: Length<Vertical>,
    dx: Length<Horizontal>,
    dy: Length<Vertical>,
}

impl Text {
    fn make_chunks(
        &self,
        node: &Node,
        acquired_nodes: &mut AcquiredNodes<'_>,
        cascaded: &CascadedValues<'_>,
        draw_ctx: &mut DrawingCtx,
        x: f64,
        y: f64,
    ) -> Vec<Chunk> {
        let mut chunks = Vec::new();

        let values = cascaded.get();
        let view_params = draw_ctx.get_view_params();
        let params = NormalizeParams::new(&values, &view_params);

        chunks.push(Chunk::new(&values, Some(x), Some(y)));

        let dx = self.dx.to_user(&params);
        let dy = self.dy.to_user(&params);

        children_to_chunks(
            &mut chunks,
            node,
            acquired_nodes,
            cascaded,
            draw_ctx,
            dx,
            dy,
            0,
        );
        chunks
    }
}

impl SetAttributes for Text {
    fn set_attributes(&mut self, attrs: &Attributes) -> ElementResult {
        for (attr, value) in attrs.iter() {
            match attr.expanded() {
                expanded_name!("", "x") => self.x = attr.parse(value)?,
                expanded_name!("", "y") => self.y = attr.parse(value)?,
                expanded_name!("", "dx") => self.dx = attr.parse(value)?,
                expanded_name!("", "dy") => self.dy = attr.parse(value)?,
                _ => (),
            }
        }

        Ok(())
    }
}

impl Draw for Text {
    fn draw(
        &self,
        node: &Node,
        acquired_nodes: &mut AcquiredNodes<'_>,
        cascaded: &CascadedValues<'_>,
        draw_ctx: &mut DrawingCtx,
        clipping: bool,
    ) -> Result<BoundingBox, RenderingError> {
        let values = cascaded.get();
        let view_params = draw_ctx.get_view_params();
        let params = NormalizeParams::new(&values, &view_params);

        let elt = node.borrow_element();

        let stacking_ctx = StackingContext::new(acquired_nodes, &elt, values.transform(), values);

        draw_ctx.with_discrete_layer(
            &stacking_ctx,
            acquired_nodes,
            values,
            clipping,
            None,
            &mut |an, dc| {
                let mut x = self.x.to_user(&params);
                let mut y = self.y.to_user(&params);

                let chunks = self.make_chunks(node, an, cascaded, dc, x, y);

                let mut measured_chunks = Vec::new();
                for chunk in &chunks {
                    measured_chunks.push(MeasuredChunk::from_chunk(chunk, dc));
                }

                let mut positioned_chunks = Vec::new();
                for chunk in &measured_chunks {
                    let chunk_x = chunk.x.unwrap_or(x);
                    let chunk_y = chunk.y.unwrap_or(y);

                    let positioned = PositionedChunk::from_measured(&chunk, dc, chunk_x, chunk_y);

                    x = positioned.next_chunk_x;
                    y = positioned.next_chunk_y;

                    positioned_chunks.push(positioned);
                }

                let mut bbox = dc.empty_bbox();

                for chunk in &positioned_chunks {
                    for span in &chunk.spans {
                        let span_bbox = span.draw(an, dc, clipping)?;
                        bbox.insert(&span_bbox);
                    }
                }

                Ok(bbox)
            },
        )
    }
}

#[derive(Default)]
pub struct TRef {
    link: Option<NodeId>,
}

impl TRef {
    fn to_chunks(
        &self,
        node: &Node,
        acquired_nodes: &mut AcquiredNodes<'_>,
        cascaded: &CascadedValues<'_>,
        chunks: &mut Vec<Chunk>,
        depth: usize,
    ) {
        if self.link.is_none() {
            return;
        }

        let link = self.link.as_ref().unwrap();

        let values = cascaded.get();
        if !values.is_displayed() {
            return;
        }

        if let Ok(acquired) = acquired_nodes.acquire(link) {
            let c = acquired.get();
            extract_chars_children_to_chunks_recursively(
                chunks,
                &c,
                Rc::new(values.clone()),
                depth,
            );
        } else {
            rsvg_log!(
                "element {} references a nonexistent text source \"{}\"",
                node,
                link,
            );
        }
    }
}

fn extract_chars_children_to_chunks_recursively(
    chunks: &mut Vec<Chunk>,
    node: &Node,
    values: Rc<ComputedValues>,
    depth: usize,
) {
    for child in node.children() {
        let values = values.clone();

        if child.is_chars() {
            child
                .borrow_chars()
                .to_chunks(&child, values, chunks, 0.0, 0.0, depth)
        } else {
            extract_chars_children_to_chunks_recursively(chunks, &child, values, depth + 1)
        }
    }
}

impl SetAttributes for TRef {
    fn set_attributes(&mut self, attrs: &Attributes) -> ElementResult {
        self.link = attrs
            .iter()
            .find(|(attr, _)| attr.expanded() == expanded_name!(xlink "href"))
            // Unlike other elements which use `href` in SVG2 versus `xlink:href` in SVG1.1,
            // the <tref> element got removed in SVG2.  So, here we still use a match
            // against the full namespaced version of the attribute.
            .and_then(|(attr, value)| NodeId::parse(value).attribute(attr).ok());

        Ok(())
    }
}

impl Draw for TRef {}

#[derive(Default)]
pub struct TSpan {
    x: Option<Length<Horizontal>>,
    y: Option<Length<Vertical>>,
    dx: Length<Horizontal>,
    dy: Length<Vertical>,
}

impl TSpan {
    fn to_chunks(
        &self,
        node: &Node,
        acquired_nodes: &mut AcquiredNodes<'_>,
        cascaded: &CascadedValues<'_>,
        draw_ctx: &mut DrawingCtx,
        chunks: &mut Vec<Chunk>,
        dx: f64,
        dy: f64,
        depth: usize,
    ) {
        let values = cascaded.get();
        if !values.is_displayed() {
            return;
        }

        let view_params = draw_ctx.get_view_params();
        let params = NormalizeParams::new(values, &view_params);

        let x = self.x.map(|l| l.to_user(&params));
        let y = self.y.map(|l| l.to_user(&params));

        let span_dx = dx + self.dx.to_user(&params);
        let span_dy = dy + self.dy.to_user(&params);

        if x.is_some() || y.is_some() {
            chunks.push(Chunk::new(values, x, y));
        }

        children_to_chunks(
            chunks,
            node,
            acquired_nodes,
            cascaded,
            draw_ctx,
            span_dx,
            span_dy,
            depth,
        );
    }
}

impl SetAttributes for TSpan {
    fn set_attributes(&mut self, attrs: &Attributes) -> ElementResult {
        for (attr, value) in attrs.iter() {
            match attr.expanded() {
                expanded_name!("", "x") => self.x = attr.parse(value)?,
                expanded_name!("", "y") => self.y = attr.parse(value)?,
                expanded_name!("", "dx") => self.dx = attr.parse(value)?,
                expanded_name!("", "dy") => self.dy = attr.parse(value)?,
                _ => (),
            }
        }

        Ok(())
    }
}

impl Draw for TSpan {}

fn to_pango_units(v: f64) -> i32 {
    (v * f64::from(pango::SCALE) + 0.5) as i32
}

impl From<FontStyle> for pango::Style {
    fn from(s: FontStyle) -> pango::Style {
        match s {
            FontStyle::Normal => pango::Style::Normal,
            FontStyle::Italic => pango::Style::Italic,
            FontStyle::Oblique => pango::Style::Oblique,
        }
    }
}

impl From<FontVariant> for pango::Variant {
    fn from(v: FontVariant) -> pango::Variant {
        match v {
            FontVariant::Normal => pango::Variant::Normal,
            FontVariant::SmallCaps => pango::Variant::SmallCaps,
        }
    }
}

impl From<FontStretch> for pango::Stretch {
    fn from(s: FontStretch) -> pango::Stretch {
        match s {
            FontStretch::Normal => pango::Stretch::Normal,
            FontStretch::Wider => pango::Stretch::Expanded, // not quite correct
            FontStretch::Narrower => pango::Stretch::Condensed, // not quite correct
            FontStretch::UltraCondensed => pango::Stretch::UltraCondensed,
            FontStretch::ExtraCondensed => pango::Stretch::ExtraCondensed,
            FontStretch::Condensed => pango::Stretch::Condensed,
            FontStretch::SemiCondensed => pango::Stretch::SemiCondensed,
            FontStretch::SemiExpanded => pango::Stretch::SemiExpanded,
            FontStretch::Expanded => pango::Stretch::Expanded,
            FontStretch::ExtraExpanded => pango::Stretch::ExtraExpanded,
            FontStretch::UltraExpanded => pango::Stretch::UltraExpanded,
        }
    }
}

impl From<FontWeight> for pango::Weight {
    fn from(w: FontWeight) -> pango::Weight {
        pango::Weight::__Unknown(w.numeric_weight().into())
    }
}

impl From<Direction> for pango::Direction {
    fn from(d: Direction) -> pango::Direction {
        match d {
            Direction::Ltr => pango::Direction::Ltr,
            Direction::Rtl => pango::Direction::Rtl,
        }
    }
}

impl From<Direction> for pango::Alignment {
    fn from(d: Direction) -> pango::Alignment {
        match d {
            Direction::Ltr => pango::Alignment::Left,
            Direction::Rtl => pango::Alignment::Right,
        }
    }
}

impl From<WritingMode> for pango::Direction {
    fn from(m: WritingMode) -> pango::Direction {
        match m {
            WritingMode::LrTb | WritingMode::Lr | WritingMode::Tb | WritingMode::TbRl => {
                pango::Direction::Ltr
            }
            WritingMode::RlTb | WritingMode::Rl => pango::Direction::Rtl,
        }
    }
}

impl From<WritingMode> for pango::Gravity {
    fn from(m: WritingMode) -> pango::Gravity {
        match m {
            WritingMode::Tb | WritingMode::TbRl => pango::Gravity::East,
            WritingMode::LrTb | WritingMode::Lr | WritingMode::RlTb | WritingMode::Rl => {
                pango::Gravity::South
            }
        }
    }
}

fn create_pango_layout(draw_ctx: &DrawingCtx, props: &FontProperties, text: &str) -> pango::Layout {
    let pango_context = pango::Context::from(draw_ctx);

    if let XmlLang(Some(ref lang)) = props.xml_lang {
        pango_context.set_language(&pango::Language::from_string(lang));
    }

    pango_context.set_base_gravity(pango::Gravity::from(props.writing_mode));

    match (props.unicode_bidi, props.direction) {
        (UnicodeBidi::Override, _) | (UnicodeBidi::Embed, _) => {
            pango_context.set_base_dir(pango::Direction::from(props.direction));
        }

        (_, direction) if direction != Direction::Ltr => {
            pango_context.set_base_dir(pango::Direction::from(direction));
        }

        (_, _) => {
            pango_context.set_base_dir(pango::Direction::from(props.writing_mode));
        }
    }

    let mut font_desc = pango_context.font_description().unwrap();
    font_desc.set_family(props.font_family.as_str());
    font_desc.set_style(pango::Style::from(props.font_style));

    // PANGO_VARIANT_SMALL_CAPS does nothing: https://gitlab.gnome.org/GNOME/pango/-/issues/566
    // see below for using the "smcp" OpenType feature for fonts that support it.
    // font_desc.set_variant(pango::Variant::from(props.font_variant));

    font_desc.set_weight(pango::Weight::from(props.font_weight));
    font_desc.set_stretch(pango::Stretch::from(props.font_stretch));

    font_desc.set_size(to_pango_units(props.font_size));

    let layout = pango::Layout::new(&pango_context);
    layout.set_auto_dir(false);
    layout.set_font_description(Some(&font_desc));

    // FIXME: For now we ignore the `line-height` property, even though we parse it.
    // We would need to do something like this:
    //
    // layout.set_line_spacing(0.0); // "actually use the spacing I'll give you"
    // layout.set_spacing(to_pango_units(???));
    //
    // However, Layout::set_spacing() takes an inter-line spacing (from the baseline of
    // one line to the top of the next line), not the line height (from baseline to
    // baseline).
    //
    // Maybe we need to implement layout of individual lines by hand.

    let attr_list = pango::AttrList::new();

    attr_list.insert(pango::Attribute::new_letter_spacing(to_pango_units(
        props.letter_spacing,
    )));

    if props.text_decoration.underline {
        attr_list.insert(pango::Attribute::new_underline(pango::Underline::Single));
    }

    if props.text_decoration.strike {
        attr_list.insert(pango::Attribute::new_strikethrough(true));
    }

    // FIXME: Using the "smcp" OpenType feature only works for fonts that support it.  We
    // should query if the font supports small caps, and synthesize them if it doesn't.
    if props.font_variant == FontVariant::SmallCaps {
        // smcp - small capitals - https://docs.microsoft.com/en-ca/typography/opentype/spec/features_pt#smcp
        attr_list.insert(pango::Attribute::new_font_features("'smcp' 1"));
    }

    layout.set_attributes(Some(&attr_list));
    layout.set_alignment(pango::Alignment::from(props.direction));
    layout.set_text(text);

    layout
}

#[cfg(test)]
mod tests {
    use super::Chars;

    #[test]
    fn chars_default() {
        let c = Chars::default();
        assert!(c.is_empty());
        assert!(c.space_normalized.borrow().is_none());
    }

    #[test]
    fn chars_new() {
        let example = "Test 123";
        let c = Chars::new(example);
        assert_eq!(c.get_string(), example);
        assert!(c.space_normalized.borrow().is_none());
    }
}
