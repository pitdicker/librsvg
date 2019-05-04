use cairo::{Matrix, MatrixTrait};
use downcast_rs::*;
use std::cell::{Cell, Ref, RefCell};
use std::collections::HashSet;
use std::rc::{Rc, Weak};

use crate::attributes::Attribute;
use crate::cond::{RequiredExtensions, RequiredFeatures, SystemLanguage};
use crate::css::CssRules;
use crate::drawing_ctx::DrawingCtx;
use crate::error::*;
use crate::parsers::Parse;
use crate::properties::{ComputedValues, SpecifiedValue, SpecifiedValues};
use crate::property_bag::PropertyBag;
use crate::property_defs::Overflow;
use crate::tree_utils;
use locale_config::Locale;

// A *const RsvgNode is just a pointer for the C code's benefit: it
// points to an  Rc<Node>, which is our refcounted Rust representation
// of nodes.
pub type RsvgNode = Rc<Node>;

/// Can obtain computed values from a node
///
/// In our tree of SVG elements (Node in our parlance), each node stores a `ComputedValues` that
/// gets computed during the initial CSS cascade.  However, sometimes nodes need to be rendered
/// outside the normal hierarchy.  For example, the `<use>` element can "instance" a subtree from
/// elsewhere in the SVG; it causes the instanced subtree to re-cascade from the computed values for
/// the `<use>` element.
///
/// This structure gets created by `Node.get_cascaded_values()`.  You can then call the `get()`
/// method on the resulting `CascadedValues` to get a `&ComputedValues` whose fields you can access.
pub struct CascadedValues<'a> {
    inner: CascadedInner<'a>,
}

enum CascadedInner<'a> {
    FromNode(Ref<'a, ComputedValues>),
    FromValues(ComputedValues),
}

impl<'a> CascadedValues<'a> {
    /// Creates a `CascadedValues` that has the same cascading mode as &self
    ///
    /// This is what nodes should normally use to draw their children from their `draw()` method.
    /// Nodes that need to override the cascade for their children can use `new_from_values()`
    /// instead.
    pub fn new(&self, node: &'a Node) -> CascadedValues<'a> {
        match self.inner {
            CascadedInner::FromNode(_) => CascadedValues {
                inner: CascadedInner::FromNode(node.data.values.borrow()),
            },

            CascadedInner::FromValues(ref v) => CascadedValues::new_from_values(node, v),
        }
    }

    /// Creates a `CascadedValues` that will hold the `node`'s computed values
    ///
    /// This is to be used only in the toplevel drawing function, or in elements like `<marker>`
    /// that don't propagate their parent's cascade to their children.  All others should use
    /// `new()` to derive the cascade from an existing one.
    fn new_from_node(node: &Node) -> CascadedValues<'_> {
        CascadedValues {
            inner: CascadedInner::FromNode(node.data.values.borrow()),
        }
    }

    /// Creates a `CascadedValues` that will override the `node`'s cascade with the specified
    /// `values`
    ///
    /// This is for the `<use>` element, which draws the element which it references with the
    /// `<use>`'s own cascade, not wih the element's original cascade.
    pub fn new_from_values(node: &'a Node, values: &ComputedValues) -> CascadedValues<'a> {
        let mut v = values.clone();
        node.data
            .specified_values
            .borrow()
            .to_computed_values(&mut v);

        CascadedValues {
            inner: CascadedInner::FromValues(v),
        }
    }

    /// Returns the cascaded `ComputedValues`.
    ///
    /// Nodes should use this from their `NodeTrait::draw()` implementation to get the
    /// `ComputedValues` from the `CascadedValues` that got passed to `draw()`.
    pub fn get(&'a self) -> &'a ComputedValues {
        match self.inner {
            CascadedInner::FromNode(ref r) => &*r,
            CascadedInner::FromValues(ref v) => v,
        }
    }
}

/// The basic trait that all nodes must implement
pub trait NodeTrait: Downcast {
    /// Sets per-node attributes from the `pbag`
    ///
    /// Each node is supposed to iterate the `pbag`, and parse any attributes it needs.
    fn set_atts(&self, node: &RsvgNode, pbag: &PropertyBag<'_>) -> NodeResult;

    /// Sets any special-cased properties that the node may have, that are different
    /// from defaults in the node's `SpecifiedValues`.
    fn set_overridden_properties(&self, _values: &mut SpecifiedValues) {}

    fn draw(
        &self,
        _node: &RsvgNode,
        _cascaded: &CascadedValues<'_>,
        _draw_ctx: &mut DrawingCtx,
        _clipping: bool,
    ) -> Result<(), RenderingError> {
        // by default nodes don't draw themselves
        Ok(())
    }
}

impl_downcast!(NodeTrait);

// After creating/parsing a Node, it will be in a success or an error state.
// We represent this with a Result, aliased as a NodeResult.  There is no
// extra information for the Ok case; all the interesting stuff is in the
// Err case.
//
// https://www.w3.org/TR/SVG/implnote.html#ErrorProcessing
//
// When an element has an error during parsing, the SVG spec calls the element
// to be "in error".  We skip rendering of elements that are in error.
//
// When we parse an element's attributes, we stop as soon as we
// encounter the first error:  a parse error, or an invalid value,
// etc.  No further attributes will be processed, although note that
// the order in which an element's attributes are processed is not
// defined.
//
// Alternatively, we could try to parse/validate all the attributes
// that come in an element and build up a Vec<NodeError>.  However, we
// don't do this now.  Doing that may be more useful for an SVG
// validator, not a renderer like librsvg is.
pub type NodeResult = Result<(), NodeError>;

pub struct NodeData {
    node_type: NodeType,
    id: Option<String>,    // id attribute from XML element
    class: Option<String>, // class attribute from XML element
    specified_values: RefCell<SpecifiedValues>,
    important_styles: RefCell<HashSet<Attribute>>,
    result: RefCell<NodeResult>,
    transform: Cell<Matrix>,
    values: RefCell<ComputedValues>,
    cond: Cell<bool>,
    node_impl: Box<NodeTrait>,
    style_attr: RefCell<String>,
}

pub type Node = tree_utils::Node<NodeData>;

#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd)]
pub enum NodeType {
    Chars,
    Circle,
    ClipPath,
    ComponentTransferFunctionA,
    ComponentTransferFunctionB,
    ComponentTransferFunctionG,
    ComponentTransferFunctionR,
    Defs,
    DistantLight,
    Ellipse,
    Filter,
    Group,
    Image,
    Line,
    LinearGradient,
    Link,
    Marker,
    Mask,
    Path,
    Pattern,
    PointLight,
    Polygon,
    Polyline,
    RadialGradient,
    Rect,
    SpotLight,
    Stop,
    Style,
    Svg,
    Switch,
    Symbol,
    Text,
    TRef,
    TSpan,
    Use,

    // Filter primitives
    FilterPrimitiveBlend,
    FilterPrimitiveColorMatrix,
    FilterPrimitiveComponentTransfer,
    FilterPrimitiveComposite,
    FilterPrimitiveConvolveMatrix,
    FilterPrimitiveDiffuseLighting,
    FilterPrimitiveDisplacementMap,
    FilterPrimitiveFlood,
    FilterPrimitiveGaussianBlur,
    FilterPrimitiveImage,
    FilterPrimitiveMerge,
    FilterPrimitiveMergeNode,
    FilterPrimitiveMorphology,
    FilterPrimitiveOffset,
    FilterPrimitiveSpecularLighting,
    FilterPrimitiveTile,
    FilterPrimitiveTurbulence,
}

impl NodeType {
    fn element_name(&self) -> &'static str {
        match self {
            NodeType::Chars => "rsvg-chars", // Dummy element name for chars
            NodeType::Circle => "circle",
            NodeType::ClipPath => "clipPath",
            NodeType::ComponentTransferFunctionA => "feFuncA",
            NodeType::ComponentTransferFunctionB => "feFuncB",
            NodeType::ComponentTransferFunctionG => "feFuncG",
            NodeType::ComponentTransferFunctionR => "feFuncR",
            NodeType::Defs => "defs",
            NodeType::DistantLight => "feDistantLight",
            NodeType::Ellipse => "ellipse",
            NodeType::Filter => "filter",
            NodeType::Group => "g",
            NodeType::Image => "image",
            NodeType::Line => "line",
            NodeType::LinearGradient => "linearGradient",
            NodeType::Link => "a",
            NodeType::Marker => "marker",
            NodeType::Mask => "mask",
            NodeType::Path => "path",
            NodeType::Pattern => "pattern",
            NodeType::PointLight => "fePointight",
            NodeType::Polygon => "polygon",
            NodeType::Polyline => "polyline",
            NodeType::RadialGradient => "radialGradient",
            NodeType::Rect => "rect",
            NodeType::SpotLight => "feSpotLight",
            NodeType::Stop => "stop",
            NodeType::Style => "style",
            NodeType::Svg => "svg",
            NodeType::Switch => "switch",
            NodeType::Symbol => "symbol",
            NodeType::Text => "text",
            NodeType::TRef => "tref",
            NodeType::TSpan => "tspan",
            NodeType::Use => "use",
            NodeType::FilterPrimitiveBlend => "feBlend",
            NodeType::FilterPrimitiveColorMatrix => "feColorMatrix",
            NodeType::FilterPrimitiveComponentTransfer => "feComponentTransfer",
            NodeType::FilterPrimitiveComposite => "feComposite",
            NodeType::FilterPrimitiveConvolveMatrix => "feConvolveMatrix",
            NodeType::FilterPrimitiveDiffuseLighting => "feDiffuseLighting",
            NodeType::FilterPrimitiveDisplacementMap => "feDisplacementMap",
            NodeType::FilterPrimitiveFlood => "feFlood",
            NodeType::FilterPrimitiveGaussianBlur => "feGaussianBlur",
            NodeType::FilterPrimitiveImage => "feImage",
            NodeType::FilterPrimitiveMerge => "feMerge",
            NodeType::FilterPrimitiveMergeNode => "feMergeNode",
            NodeType::FilterPrimitiveMorphology => "feMorphology",
            NodeType::FilterPrimitiveOffset => "feOffset",
            NodeType::FilterPrimitiveSpecularLighting => "feSpecularLighting",
            NodeType::FilterPrimitiveTile => "feTile",
            NodeType::FilterPrimitiveTurbulence => "feTurbulence",
        }
    }
}

impl Node {
    pub fn new(
        node_type: NodeType,
        parent: Option<Weak<Node>>,
        id: Option<&str>,
        class: Option<&str>,
        node_impl: Box<NodeTrait>,
    ) -> Node {
        let data = NodeData {
            node_type,
            id: id.map(str::to_string),
            class: class.map(str::to_string),
            specified_values: RefCell::new(Default::default()),
            important_styles: Default::default(),
            transform: Cell::new(Matrix::identity()),
            result: RefCell::new(Ok(())),
            values: RefCell::new(ComputedValues::default()),
            cond: Cell::new(true),
            node_impl,
            style_attr: RefCell::new(String::new()),
        };

        tree_utils::Node::<NodeData> {
            parent,
            first_child: RefCell::new(None),
            last_child: RefCell::new(None),
            next_sib: RefCell::new(None),
            prev_sib: RefCell::new(None),
            data,
        }
    }

    pub fn get_type(&self) -> NodeType {
        self.data.node_type
    }

    pub fn get_id(&self) -> Option<&str> {
        self.data.id.as_ref().map(String::as_str)
    }

    pub fn get_class(&self) -> Option<&str> {
        self.data.class.as_ref().map(String::as_str)
    }

    pub fn get_human_readable_name(&self) -> String {
        format!(
            "{:?} id={}",
            self.get_type(),
            self.get_id().unwrap_or("None")
        )
    }

    pub fn get_transform(&self) -> Matrix {
        self.data.transform.get()
    }

    pub fn get_cascaded_values(&self) -> CascadedValues<'_> {
        CascadedValues::new_from_node(self)
    }

    pub fn cascade(&self, values: &ComputedValues) {
        let mut values = values.clone();
        self.data
            .specified_values
            .borrow()
            .to_computed_values(&mut values);
        *self.data.values.borrow_mut() = values.clone();

        for child in self.children() {
            child.cascade(&values);
        }
    }

    pub fn set_styles_recursively(&self, css_rules: &CssRules) {
        self.set_style(css_rules);

        for child in self.children() {
            child.set_styles_recursively(css_rules);
        }
    }

    pub fn get_cond(&self) -> bool {
        self.data.cond.get()
    }

    fn set_transform_attribute(&self, pbag: &PropertyBag<'_>) -> Result<(), NodeError> {
        for (attr, value) in pbag.iter() {
            match attr {
                Attribute::Transform => {
                    return Matrix::parse_str(value)
                        .attribute(Attribute::Transform)
                        .and_then(|affine| Ok(self.data.transform.set(affine)));
                }

                _ => (),
            }
        }

        Ok(())
    }

    fn save_style_attribute(&self, pbag: &PropertyBag<'_>) {
        let mut style_attr = self.data.style_attr.borrow_mut();

        for (attr, value) in pbag.iter() {
            match attr {
                Attribute::Style => style_attr.push_str(value),

                _ => (),
            }
        }
    }

    pub fn set_atts(&self, node: &RsvgNode, pbag: &PropertyBag<'_>, locale: &Locale) {
        self.save_style_attribute(pbag);

        if let Err(e) = self
            .set_transform_attribute(pbag)
            .and_then(|_| self.parse_conditional_processing_attributes(pbag, locale))
            .and_then(|_| self.data.node_impl.set_atts(node, pbag))
            .and_then(|_| self.set_presentation_attributes(pbag))
        {
            self.set_error(e);
        }
    }

    fn parse_conditional_processing_attributes(
        &self,
        pbag: &PropertyBag<'_>,
        locale: &Locale,
    ) -> Result<(), NodeError> {
        let mut cond = self.get_cond();

        for (attr, value) in pbag.iter() {
            // FIXME: move this to "try {}" when we can bump the rustc version dependency
            let mut parse = || {
                match attr {
                    Attribute::RequiredExtensions if cond => {
                        cond = RequiredExtensions::from_attribute(value)
                            .map(|RequiredExtensions(res)| res)?;
                    }

                    Attribute::RequiredFeatures if cond => {
                        cond = RequiredFeatures::from_attribute(value)
                            .map(|RequiredFeatures(res)| res)?;
                    }

                    Attribute::SystemLanguage if cond => {
                        cond = SystemLanguage::from_attribute(value, locale)
                            .map(|SystemLanguage(res)| res)?;
                    }

                    _ => {}
                }

                Ok(cond)
            };

            parse()
                .map(|c| self.data.cond.set(c))
                .map_err(|e| NodeError::attribute_error(attr, e))?;
        }

        Ok(())
    }

    /// Hands the pbag to the node's state, to apply the presentation attributes
    fn set_presentation_attributes(&self, pbag: &PropertyBag<'_>) -> Result<(), NodeError> {
        match self
            .data
            .specified_values
            .borrow_mut()
            .parse_presentation_attributes(pbag)
        {
            Ok(_) => Ok(()),
            Err(e) => {
                // FIXME: we'll ignore errors here for now.
                //
                // If we set the node to be in error, we expose buggy handling of the
                // enable-background property; we are not parsing it correctly. This
                // causes tests/fixtures/reftests/bugs/587721-text-transform.svg to fail
                // because it has enable-background="new 0 0 1179.75118 687.74173" in the
                // toplevel svg element.
                //
                //   self.set_error(e);
                //   return;

                rsvg_log!("(attribute error: {})", e);
                Ok(())
            }
        }
    }

    /// Implements a very limited CSS selection engine
    fn set_css_styles(&self, css_rules: &CssRules) {
        // Try to properly support all of the following, including inheritance:
        // *
        // #id
        // tag
        // tag#id
        // tag.class
        // tag.class#id
        //
        // This is basically a semi-compliant CSS2 selection engine

        let element_name = self.get_type().element_name();
        let mut specified_values = self.data.specified_values.borrow_mut();
        let mut important_styles = self.data.important_styles.borrow_mut();

        // *
        try_apply_by_selector(css_rules, "*", &mut specified_values, &mut important_styles);

        // tag
        try_apply_by_selector(css_rules, element_name, &mut specified_values, &mut important_styles);

        if let Some(klazz) = self.get_class() {
            for cls in klazz.split_whitespace() {
                let mut found = false;

                if !cls.is_empty() {
                    // tag.class#id
                    if let Some(id) = self.get_id() {
                        let target = format!("{}.{}#{}", element_name, cls, id);
                        found = found
                            || try_apply_by_selector(
                                css_rules,
                                &target,
                                &mut specified_values,
                                &mut important_styles,
                            );
                    }

                    // .class#id
                    if let Some(id) = self.get_id() {
                        let target = format!(".{}#{}", cls, id);
                        found = found
                            || try_apply_by_selector(
                                css_rules,
                                &target,
                                &mut specified_values,
                                &mut important_styles,
                            );
                    }

                    // tag.class
                    let target = format!("{}.{}", element_name, cls);
                    found = found
                        || try_apply_by_selector(
                            css_rules,
                            &target,
                            &mut specified_values,
                            &mut important_styles,
                        );

                    if !found {
                        // didn't find anything more specific, just apply the class style
                        let target = format!(".{}", cls);
                        try_apply_by_selector(
                            css_rules,
                            &target,
                            &mut specified_values,
                            &mut important_styles,
                        );
                    }
                }
            }
        }

        if let Some(id) = self.get_id() {
            // id
            let target = format!("#{}", id);
            try_apply_by_selector(css_rules, &target, &mut specified_values, &mut important_styles);

            // tag#id
            let target = format!("{}#{}", element_name, id);
            try_apply_by_selector(css_rules, &target, &mut specified_values, &mut important_styles);
        }
    }

    /// Applies CSS styles from the saved value of the "style" attribute
    fn set_style_attribute(&self) {
        let mut style_attr = self.data.style_attr.borrow_mut();

        if !style_attr.is_empty() {
            let mut important_styles = self.data.important_styles.borrow_mut();

            if let Err(e) = self
                .data
                .specified_values
                .borrow_mut()
                .parse_style_declarations(style_attr.as_str(), &mut important_styles)
            {
                self.set_error(e);
            }

            style_attr.clear();
            style_attr.shrink_to_fit();
        }
    }

    // Sets the node's specified values from the style-related attributes in the pbag.
    // Also applies CSS rules in our limited way based on the node's tag/class/id.
    fn set_style(&self, css_rules: &CssRules) {
        self.set_css_styles(css_rules);
        self.set_style_attribute();
    }

    pub fn set_overridden_properties(&self) {
        let mut specified_values = self.data.specified_values.borrow_mut();
        self.data
            .node_impl
            .set_overridden_properties(&mut specified_values);
    }

    pub fn draw(
        &self,
        node: &RsvgNode,
        cascaded: &CascadedValues<'_>,
        draw_ctx: &mut DrawingCtx,
        clipping: bool,
    ) -> Result<(), RenderingError> {
        if !self.is_in_error() {
            draw_ctx.with_saved_matrix(&mut |dc| {
                let cr = dc.get_cairo_context();
                cr.transform(self.get_transform());

                self.data.node_impl.draw(node, cascaded, dc, clipping)
            })
        } else {
            rsvg_log!(
                "(not rendering element {} because it is in error)",
                self.get_human_readable_name()
            );

            Ok(()) // maybe we should actually return a RenderingError::NodeIsInError here?
        }
    }

    pub fn set_error(&self, error: NodeError) {
        rsvg_log!(
            "(attribute error for {}:\n  {})",
            self.get_human_readable_name(),
            error
        );

        *self.data.result.borrow_mut() = Err(error);
    }

    pub fn is_in_error(&self) -> bool {
        self.data.result.borrow().is_err()
    }

    pub fn with_impl<T, F, U>(&self, f: F) -> U
    where
        T: NodeTrait,
        F: FnOnce(&T) -> U,
    {
        if let Some(t) = (&self.data.node_impl).downcast_ref::<T>() {
            f(t)
        } else {
            panic!("could not downcast");
        }
    }

    pub fn get_impl<T: NodeTrait>(&self) -> Option<&T> {
        (&self.data.node_impl).downcast_ref::<T>()
    }

    pub fn draw_children(
        &self,
        cascaded: &CascadedValues<'_>,
        draw_ctx: &mut DrawingCtx,
        clipping: bool,
    ) -> Result<(), RenderingError> {
        for child in self.children() {
            draw_ctx.draw_node_from_stack(
                &CascadedValues::new(cascaded, &child),
                &child,
                clipping,
            )?;
        }

        Ok(())
    }

    pub fn is_overflow(&self) -> bool {
        self.data.specified_values.borrow().is_overflow()
    }

    pub fn set_overflow_hidden(&self) {
        let mut specified_values = self.data.specified_values.borrow_mut();
        specified_values.overflow = SpecifiedValue::Specified(Overflow::Hidden);
    }

    // find the last Chars node so that we can coalesce
    // the text and avoid screwing up the Pango layouts
    pub fn find_last_chars_child(&self) -> Option<Rc<Node>> {
        for child in self.children().rev() {
            match child.get_type() {
                NodeType::Chars => return Some(child),

                _ => return None,
            }
        }

        None
    }
}

pub fn node_new(
    node_type: NodeType,
    parent: Option<&RsvgNode>,
    id: Option<&str>,
    class: Option<&str>,
    node_impl: Box<NodeTrait>,
) -> RsvgNode {
    Rc::new(Node::new(
        node_type,
        if let Some(parent) = parent {
            Some(Rc::downgrade(parent))
        } else {
            None
        },
        id,
        class,
        node_impl,
    ))
}

/// takes CSS rules which match the given `selector` name and applies them
/// to the `values`.
pub fn try_apply_by_selector(
    css_rules: &CssRules,
    selector: &str,
    values: &mut SpecifiedValues,
    important_styles: &mut HashSet<Attribute>,
) -> bool {
    if let Some(decl_list) = css_rules.lookup(selector) {
        for declaration in decl_list.iter() {
            values.set_property_from_declaration(declaration, important_styles);
        }

        true
    } else {
        false
    }
}
